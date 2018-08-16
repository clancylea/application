/*
 *  Copyright 2018 Digital Media Professionals Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "imagenet_1000_categories.h"

#include "CaffeGoogLeNet_gen.h"
#include "util_draw.h"
#include "util_input.h"
CCaffeGoogLeNet network;

#define FILENAME_WEIGHTS "CaffeGoogLeNet_weights.bin"

using namespace std;

#define SCREEN_W 1280
#define SCREEN_H 720

#define IMAGE_W 224
#define IMAGE_H 224

#define TEXT_XOFS (((SCREEN_W - IMAGE_W) / 2) / 24 + 4)  // 8x8 characters
#define TEXT_YOFS ((512 + 48) / 8 + 2 + 3 + 2)           // 8x8 characters

#define BARS_XOFS (512)       // pixels
#define BARS_YOFS (512 + 48)  // pixels

std::vector<std::string> catstr_vec(categories, categories + 1000);

unsigned int fc = 0;

unsigned int imgView[IMAGE_W * IMAGE_H];
unsigned short imgProc[IMAGE_W * IMAGE_H * 3];

// 2ND THREAD FOR HW CONTROL

volatile uint64_t sync_cnn_in = 0;
volatile uint64_t sync_cnn_out = 0;

volatile int conv_time_tot = 0;
volatile int fc_time_tot = 0;

void* hwacc_thread_func(void* targ) {
  while (true) {
    while (sync_cnn_in == sync_cnn_out) {
      usleep(1000);  // sleep 1 ms
    }

    network.RunNetwork();

    conv_time_tot = network.get_conv_usec();
    fc_time_tot = network.get_fc_usec();

    sync_cnn_out++;
  }

  return NULL;  // will never reach here but this removes compiler warning...
}

int main(int argc, char** argv) {
  const std::string input_image_path = "./images/";
  const std::vector<std::string> input_image_suffix = {".jpg", ".jpeg", ".JPG",
                                                       ".JPEG"};

  vector<string> image_names =
      dmp::util::get_input_image_names(input_image_path, input_image_suffix);
  int num_images = image_names.size();
  if (num_images == 0) {
    cout << "No input images." << endl;
    return 1;
  }

  std::vector<float> networkOutput;

  int my_number = 0;
  bool has_democonf = false;
  if (argc >= 2) {
    my_number = atoi(argv[1]);
    has_democonf = true;
  }
  vector<pair<int, string> > democonf;
  int democonf_sel = 0;  // currencly selected element index (0 .. num-1)
  int democonf_num = 0;  // number of elements in democonf
  int democonf_string_max = -1;
  bool democonf_display = false;
  if (has_democonf) {
    ifstream democonf_file("democonf.txt");
    int count = 0;
    int c_int;
    string c_string;
    while (democonf_file >> c_int >> c_string) {
      democonf.push_back(make_pair(c_int, c_string));
      int c_string_size = c_string.size();
      if (c_string_size > democonf_string_max)
        democonf_string_max = c_string_size;
      if (c_int == my_number) democonf_sel = count;
      count++;
    }
    democonf_num = count;
  }

  dmp::util::set_inputImageSize(IMAGE_W, IMAGE_H);
  dmp::util::createBackgroundImage(SCREEN_W, SCREEN_H);

  if (!dmp::util::load_background_image("fpgatitle_googleNet.ppm")) return 1;

  //dmp::modules::initialize();

  string conv_freq, fc_freq;
  conv_freq = std::to_string(network.get_dv_info().conv_freq);
  fc_freq = std::to_string(network.get_dv_info().fc_freq);

  network.Verbose(0);
  if (!network.Initialize()) {
    return -1;
  }
  network.LoadWeights(FILENAME_WEIGHTS);

  void* ddr_buf_a_cpu = network.get_network_input_addr_cpu();

  //dmp::modules::reset_button_state();

  int exit_code = -1;

  int image_nr = 0;

  bool pause = false;

  pthread_t hwacc_thread;
  pthread_create(&hwacc_thread, NULL, hwacc_thread_func, NULL);

  while (exit_code == -1) {
    // Static Images
    if (fc < 2) {
      //dmp::util::print_background_image_toDisplay();
      //dmp::modules::swap_buffer();
      fc++;  // Frame Counter
      continue;
    }

    // HW processing times
    if (conv_time_tot != 0 && fc_time_tot != 0) {
      dmp::util::print_time_toDisplay(
          TEXT_XOFS, TEXT_YOFS + 0,
          "Convolution (" + conv_freq + " MHz HW ACC)     : ", conv_time_tot,
          9999, 0xff00ff00, 0x00000001);
      dmp::util::print_time_toDisplay(
          TEXT_XOFS, TEXT_YOFS + 2,
          "Fully Connected (" + fc_freq + " MHz HW ACC) : ", fc_time_tot, 9999,
          0xff00ff00, 0x00000001);
      dmp::util::print_time_toDisplay(
          TEXT_XOFS, TEXT_YOFS + 4,
          "Total Processing Time           : ", conv_time_tot + fc_time_tot,
          9999, 0xffff0000, 0x00000001);
    }

    if (sync_cnn_out == sync_cnn_in) {
      if (sync_cnn_out != 0) {
        network.get_final_output(networkOutput);

        dmp::util::print_image_toDisplay((SCREEN_W - IMAGE_W) / 2,
                                         (293 - 128) + 20, imgView);
        dmp::util::print_result(catstr_vec, TEXT_XOFS, TEXT_YOFS - 8 - 5,
            dmp::util::catrank(&networkOutput.front()), 0x88ff8800, 0x00ff0000,
                     0x00000001);

        //dmp::modules::swap_buffer();
        fc++;

        unsigned int button = 0;//dmp::modules::get_button_state();
        if (button & 4) {  // exit demo with exit code of selected next demo
          if (has_democonf) {
            int sel_num = democonf[democonf_sel].first;
            if (sel_num != my_number) exit_code = sel_num;
          } else {
            exit_code = my_number;
          }
        }
        if (button & 2) {  // cycle through demo configuratom list
          if (has_democonf) {
            democonf_display = true;
            if (democonf_sel == democonf_num - 1)
              democonf_sel = 0;
            else
              democonf_sel++;
          }
        }
        if (button & 1) pause = !pause;
      }

      if (!pause) {
        dmp::util::decode_jpg_file(input_image_path + image_names[image_nr],
                                   imgView, IMAGE_W, IMAGE_H);
        dmp::util::preproc_image(imgView, imgProc, IMAGE_W, IMAGE_H, -128.0,
                                 -128.0, -128.0, 1.0, true);

        if (image_nr == num_images - 1) {
          image_nr = 0;
        } else {
          image_nr++;
        }
      }

      // Copy image to FPGA memory
      memcpy(ddr_buf_a_cpu, (void*)imgProc, IMAGE_W * IMAGE_H * 3 * 2);

      if (exit_code == -1)  // do not start new HW ACC runs if about to exit...
        sync_cnn_in++;
    }

    if (democonf_display) {
      string s = democonf[democonf_sel].second;
      s.resize(democonf_string_max, ' ');
      dmp::util::print8x8_toDisplay((SCREEN_W / 8 - democonf_string_max) / 2,
                                    SCREEN_H / 8 - 1, s, 0x00ff0000,
                                    0x00000001);
    }
  }

  //dmp::modules::shutdown();

  return exit_code;
}
