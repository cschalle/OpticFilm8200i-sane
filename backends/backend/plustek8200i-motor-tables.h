/* sane - Scanner Access Now Easy.

   Copyright (C) 2025 Christian Schaller

   This file is part of the SANE package.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, see
   <https://www.gnu.org/licenses/>.

   SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef PLUSTEK8200I_MOTOR_TABLES_H
#define PLUSTEK8200I_MOTOR_TABLES_H

#include "plustek8200i.h"

/* Motor tables for the Plustek OpticFilm 8200i (acceleration curves in scanner memory) */

/* Write the motor acceleration tables to scanner memory via DMA; `steps` is currently unused */
SANE_Status plustek8200i_load_motor_tables(int fd, int steps);

#endif /* PLUSTEK8200I_MOTOR_TABLES_H */
