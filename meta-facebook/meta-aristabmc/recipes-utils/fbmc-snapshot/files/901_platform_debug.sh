#!/bin/bash
#
# Copyright 2026-present Facebook. All Rights Reserved.
#
# This program file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program in a file named COPYING; if not, write to the
# Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301 USA
#

echo -e "\n################################"
echo "##### SWITCHCARD DEBUG INFO #####"
echo "################################"
echo "##### DS4520 MODE SELECTOR I2CDUMP #####"
i2cdump -f -y 8 0x52 b

echo -e "\n################################"
echo "##### SUPERVISOR DEBUG INFO #####"
echo "################################"
echo "##### CPU CPLD VERSION #####"
if [ -x /usr/local/bin/cpld_ver.sh ]; then
    /usr/local/bin/cpld_ver.sh
else
    echo "/usr/local/bin/cpld_ver.sh doesn't exist!"
fi
echo "##### CPU POWER CPLD I2CDUMP #####"
i2cdump -f -y 12 0x43 b
