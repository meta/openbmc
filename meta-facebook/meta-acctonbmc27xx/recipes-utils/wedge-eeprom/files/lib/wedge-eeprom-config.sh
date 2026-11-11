#!/bin/bash
#
# Copyright (c) Meta Platforms, Inc. and affiliates.
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

EEPROM_CONFIG_FILE=/etc/weutil/eeprom.json
SCM_EEPROM_CONFIG_FILE=/etc/weutil/scm-eeprom.json

# Check Scm EEPROM
if i2cget -y -f 14 0x56 > /dev/null 2>&1; then
	printf 'Detected scm eeprom, use %s to replace %s.\n' \
	"$SCM_EEPROM_CONFIG_FILE" "$EEPROM_CONFIG_FILE"

	cp -f $SCM_EEPROM_CONFIG_FILE $EEPROM_CONFIG_FILE
else
	printf "No scm eeprom, skip.\n"
fi
