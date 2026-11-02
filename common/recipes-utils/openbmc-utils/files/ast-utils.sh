#!/bin/bash
#
# Copyright 2015-present Facebook. All Rights Reserved.
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


# Store and Clear POR bit
store_clear_por() {
  if [ -f /tmp/ast_por ]; then
    return
  fi

  soc_model=$(cat /etc/soc_model 2>/dev/null)

  # default AST G4/G5
  system_reset_reg="0x1e6e203c"
  if [ "$soc_model" = "SOC_MODEL_ASPEED_G7" ]; then
      # No G7 equivalent of the legacy SCU reset flag is established here.
      # Do not classify an unknown reset as a power-on reset.
      echo 0 > /tmp/ast_por
      return 0
  elif [ "$soc_model" = "SOC_MODEL_ASPEED_G6" ]; then
      system_reset_reg="0x1e6e2074"
  fi

  # Read the SRST Rest Flag
  val=$(devmem "$system_reset_reg" 2>/dev/null)
  if [[ "$((val & 0x1))" == "1" ]]; then
      # Power ON Reset
      echo 1 > /tmp/ast_por
  else
      echo 0 > /tmp/ast_por
  fi

  # Clear Power On Reset bit
  devmem "$system_reset_reg" 32 0x1 2>/dev/null
}

# Check to see if BMC power-on-reset
is_bmc_por() {
  store_clear_por
  /bin/cat /tmp/ast_por
}

# set LPC signal strength to weakest-strongest
# support only for AST G6
setup_LPC_signal_strength()
{
    soc_model=$(cat /etc/soc_model 2>/dev/null)
    if [ "$soc_model" != "SOC_MODEL_ASPEED_G6" ]; then
        echo "Error: This function is only compatible with AST G6 models." 1>&2
        return 1
    fi

    strength=$1
    if [[ $strength -lt 0 || $strength -gt 3 ]]; then
        echo "Invalid strength value. Valid range: 0-3 (weakest-strongest)" 1>&2
        return 1
    fi
    SCU=0x1E6E2000
    SCU454=$((SCU + 0x454))
    SCU458=$((SCU + 0x458))

    # SCU454 : Multi-function Pin Control #15
    # 31:30 - LAD3/ESPID3 Driving Strength
    # 29:28 - LAD2/ESPID2 Driving Strength
    # 27:26 - LAD1/ESPID1 Driving Strength
    # 25:24 - LAD0/ESPID0 Driving Strength
    #          3: strongest    0: weakest
    # set strength value to 0
    value=$(devmem "$SCU454")
    value=$(( value & 0x00FFFFFF ))
    value=$(( value | (strength << 30) ))
    value=$(( value | (strength << 28) ))
    value=$(( value | (strength << 26) ))
    value=$(( value | (strength << 24) ))
    devmem "$SCU454" 32 "$value"
    
    # SCU458 : Multi-function Pin Control #16
    # 19:18 - LPCRSTN/ESPIRSTN Driving Strength
    # 17:16 - LACDIRQ/ESPIALERT Driving Strength
    # 15:14 - LPCFRAMEN/ESPICSN Driving Strength
    # 13:12 - LPCCLK/ESPICLK Driving Strength
    #          3: strongest    0: weakest
    # set strength value to 0
    value=$(devmem "$SCU458")
    value=$(( value & 0xFFF00FFF ))
    value=$(( value | (strength << 18) ))
    value=$(( value | (strength << 16) ))
    value=$(( value | (strength << 14) ))
    value=$(( value | (strength << 12) ))
    devmem "$SCU458" 32 "$value"
}
