#!/bin/bash
#
# Copyright 2023-present Facebook. All Rights Reserved.
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

# shellcheck disable=SC1091
. /usr/local/bin/openbmc-utils.sh

NETWORK_CONF_FILE="/etc/systemd/network/10-eth0.network"

if wedge_variable_smb_eeprom_addr; then
    # Adjust the smb target device address if alternate address is in use
    if [ -f "/sys/bus/i2c/drivers/at24/9-0057/eeprom" ]; then
        echo "Found SMB eeprom at 0x57, adjusting eeprom.json"
        sed -i 's|9-0052/eeprom|9-0057/eeprom|g' /etc/weutil/eeprom.json
    fi
fi

cpu_id=$(wedge_cpu_id)

if [[ "$cpu_id" == "$BOARD_ID_RUGGLES" || \
      "$cpu_id" == "$BOARD_ID_ICECUBE" ]]; then
    # On Ruggles and Marconi, alias smb to 9-0050.
    sed -i '/"name" : "smb"/ {N; s/9-0052/9-0050/}' /etc/weutil/eeprom.json
fi

if [ -f "/sys/bus/i2c/drivers/at24/9-0053/eeprom" ]; then
    echo "Found chassis EEPROM at 0x53, adjusting eeprom.json"
    sed -i '/"name" : "chassis_eeprom"/ {N; s/9-0052/9-0053/}' /etc/weutil/eeprom.json
fi

smb_eeprom_sysfs=$(smb_eeprom_sysfs_path)
if [ ! -e "$smb_eeprom_sysfs" ]; then
    echo "No SMB eeprom found"
    exit 0
fi

if [ -f "/sys/bus/i2c/drivers/at24/9-0053/eeprom" ]; then
    echo "Found chassis EEPROM at 0x53, adjusting eeprom.json"
    sed -i '/"name" : "chassis_eeprom"/ {N; s/9-0052/9-0053/}' /etc/weutil/eeprom.json
fi

product=$($WEUTIL_CMD smb 2>&1 | awk -F': ' '/Product Name:/ {print $2}')

if [ "${product^^}" != "MERU800BFA" ]; then
    # No need to configure vlan 4092 for other products.
    exit 0
fi

echo "Configuring VLAN 4092"
vlan4088cfg="VLAN=eth0.4088"
vlan4092cfg="VLAN=eth0.4092"
sed -i "s/$vlan4088cfg/$vlan4088cfg\n$vlan4092cfg/" "$NETWORK_CONF_FILE"
cp /etc/meru-21-eth0.4092.network /etc/systemd/network/21-eth0.4092.network
cp /etc/meru-21-eth0.4092.netdev /etc/systemd/network/21-eth0.4092.netdev
