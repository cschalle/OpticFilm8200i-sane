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

#ifndef BACKEND_GENESYS_FWD_H
#define BACKEND_GENESYS_FWD_H

namespace genesys {

// calibration.h
struct Genesys_Calibration_Cache;

// command_set.h
class CommandSet;

// device.h
struct Genesys_Gpo;
struct MethodResolutions;
struct Genesys_Model;
struct Genesys_Device;

// error.h
class DebugMessageHelper;
class SaneException;

// genesys.h
class GenesysButton;
struct Genesys_Scanner;

// image.h
class Image;

// image_buffer.h
class ImageBuffer;

// image_pipeline.h
class ImagePipelineNode;
// ImagePipelineNode* skipped
class ImagePipelineStack;

// image_pixel.h
struct Pixel;
struct RawPixel;

// low.h
class UsbDeviceEntry;

// motor.h
struct Genesys_Motor;
struct MotorSlope;
struct MotorProfile;
struct MotorSlopeTable;

// register.h
class Genesys_Register_Set;
struct GenesysRegisterSetState;

// row_buffer.h
class RowBuffer;

// usb_device.h
class IUsbDevice;
class UsbDevice;

// scanner_interface.h
class ScannerInterface;
class ScannerInterfaceUsb;
class TestScannerInterface;

// sensor.h
struct GenesysFrontendLayout;
struct Genesys_Frontend;
struct SensorExposure;
struct Genesys_Sensor;

// settings.h
struct Genesys_Settings;
struct SetupParams;
struct ScanSession;

// value_filter.h
template<class T> class ValueFilter;
template<class T> class ValueFilterAny;

// test_usb_device.h
class TestUsbDevice;

} // namespace genesys

#endif
