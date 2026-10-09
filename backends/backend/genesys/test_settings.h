/* sane - Scanner Access Now Easy.

   Copyright (C) 2019 Povilas Kanapickas <povilas@radix.lt>

   This file is part of the SANE package.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef BACKEND_GENESYS_TEST_SETTINGS_H
#define BACKEND_GENESYS_TEST_SETTINGS_H

#include "scanner_interface.h"
#include "register_cache.h"
#include "test_usb_device.h"
#include <functional>

namespace genesys {

using TestCheckpointCallback = std::function<void(const Genesys_Device&,
                                                  TestScannerInterface&,
                                                  const std::string&)>;

bool is_testing_mode();
void disable_testing_mode();
void enable_testing_mode(std::uint16_t vendor_id, std::uint16_t product_id,
                         std::uint16_t bcd_device,
                         TestCheckpointCallback checkpoint_callback);
std::uint16_t get_testing_vendor_id();
std::uint16_t get_testing_product_id();
std::uint16_t get_testing_bcd_device();
std::string get_testing_device_name();
TestCheckpointCallback get_testing_checkpoint_callback();


} // namespace genesys

#endif // BACKEND_GENESYS_TEST_SETTINGS_H
