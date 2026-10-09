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

#define DEBUG_DECLARE_ONLY

#include "static_init.h"
#include <vector>

namespace genesys {

static std::unique_ptr<std::vector<std::function<void()>>> s_functions_run_at_backend_exit;

void add_function_to_run_at_backend_exit(const std::function<void()>& function)
{
    if (!s_functions_run_at_backend_exit)
        s_functions_run_at_backend_exit.reset(new std::vector<std::function<void()>>());
    s_functions_run_at_backend_exit->push_back(std::move(function));
}

void run_functions_at_backend_exit()
{
    if (s_functions_run_at_backend_exit)
    {
        for (auto it = s_functions_run_at_backend_exit->rbegin();
             it != s_functions_run_at_backend_exit->rend(); ++it)
        {
            (*it)();
        }
        s_functions_run_at_backend_exit.reset();
    }
}

} // namespace genesys
