#!/bin/bash

is_rcvy=false
if [[ ($# -eq 5 || $# -eq 4) && "$2" == "--rcvy" ]]; then
    is_rcvy=true
fi

# shellcheck source=meta-facebook/meta-yosemite4/recipes-yosemite4/plat-tool/files/yosemite4-common-functions
source /usr/libexec/yosemite4-common-functions

# Exit code constants
PRECHECK_BMC_UPTIME_TOO_SHORT=10
PRECHECK_PLDMD_INACTIVE=11
PRECHECK_PLDMD_JUST_STARTED_MS_OR_S=12
PRECHECK_PLDMD_JUST_STARTED_MIN=13

check_uptime_and_pldmd_conservative() {
	echo "=== Pre-check (conservative) ==="

	local uptime_line up_part uptime_min=""
	uptime_line=$(uptime)
	echo "BMC uptime: $uptime_line"
	up_part=$(echo "$uptime_line" | sed -E 's/.* up[[:space:]]+([^,]+).*/\1/')

	if echo "$up_part" | grep -Eq '(^|[^0-9A-Za-z])[0-9]+[[:space:]]*min(s)?([^0-9A-Za-z]|$)'; then
		uptime_min=$(echo "$up_part" \
			| grep -Eo '(^|[^0-9A-Za-z])[0-9]+[[:space:]]*min(s)?([^0-9A-Za-z]|$)' \
			| grep -Eo '[0-9]+' \
			| head -n1)
		if [ -n "$uptime_min" ] && [ "$uptime_min" -lt 7 ]; then
			echo "BLOCK: BMC just booted (uptime ${uptime_min} min < 7 min). Please wait at least 7 minutes before running the update."
			sleep 60
			exit $PRECHECK_BMC_UPTIME_TOO_SHORT
		fi
	fi

	if [ $# -eq 4 ] && [ "$2" == "--rcvy" ]; then
		echo "Recover and then update one BIC by uart mode, skip pldmd status check."
		echo "Pre-check passed (conservative)."
		echo "==============================="
		return 0
	fi

	if ! systemctl is-active --quiet pldmd; then
		echo "BLOCK: pldmd is not active. Please run 'systemctl start pldmd' and retry after about 3 minutes."
		exit $PRECHECK_PLDMD_INACTIVE
	fi

	local active_line ago_text
	active_line=$(systemctl status pldmd 2>/dev/null | grep -E 'Active:' | head -n1)
	echo "pldmd status (systemd): $active_line"
	ago_text=$(echo "$active_line" | sed -nE 's/.*;\s*([^)]*ago).*/\1/p')

	if [ -n "$ago_text" ]; then
		if echo "$ago_text" | grep -Eq '([0-9]+)[[:space:]]*(h|hr|hrs|hour|hours|d|day|days|w|week|weeks|month|months|y|year|years)\b'; then
			:
		else
			unit_match=$(echo "$ago_text" | grep -Eo '(^|[^0-9A-Za-z])([0-9]+)[[:space:]]*(ms|s|min|mins)([^0-9A-Za-z]|$)' | head -n1)
			if [ -n "$unit_match" ]; then
				num=$(echo "$unit_match" | grep -Eo '[0-9]+' | head -n1)
				unit=$(echo "$unit_match" | grep -Eo '(ms|s|min|mins)' | head -n1)

				case "$unit" in
				ms|s)
					echo "BLOCK: pldmd just started very recently (${ago_text}). Please wait about 3 minutes and retry."
					sleep 60
					exit $PRECHECK_PLDMD_JUST_STARTED_MS_OR_S
					;;
				min|mins)
					if [ -n "$num" ] && [ "$num" -lt 3 ]; then
						echo "BLOCK: pldmd just started (${num} min ago < 3 min). Please wait about 3 minutes and retry."
						sleep 60
						exit $PRECHECK_PLDMD_JUST_STARTED_MIN
					fi
					;;
				esac
			fi
		fi
	fi


	echo "Pre-check passed (conservative)."
	echo "==============================="
	return 0
}

check_uptime_and_pldmd_conservative "$@"

RETRY_UPDATE_COUNT=200
MAX_RETRIES=3
lockfile="/tmp/pldm-fw-update.lock"

# pldm ComponentIdentifier
readonly SD_S3_VR_ID="02" # SD VR PVDD11_S3
readonly SD_CPU0_VR_ID="03" # SD VR PVDDCR_CPU0
readonly SD_CPU1_VR_ID="01" # SD VR PVDDCR_CPU1
readonly WF_VR_INF_AB_CXL1="01" # WF VR PVDDQ_AB 1
readonly WF_VR_INF_AB_CXL2="03" # WF VR PVDDQ_AB 2
readonly WF_VR_INF_CD_CXL1="02" # WF VR PVDDQ_CD 1
readonly WF_VR_INF_CD_CXL2="04" # WF VR PVDDQ_CD 2

# phosphor-dbus-interfaces define
readonly TargetDetermined="xyz.openbmc_project.Software.Update.TargetDetermined"
readonly UpdateSuccessful="xyz.openbmc_project.Software.Update.UpdateSuccessful"
readonly ActivateFailed="xyz.openbmc_project.Software.Update.ActivateFailed"

exec 200>"$lockfile"
retry_remain_count=$RETRY_UPDATE_COUNT
echo ""
while true
do
    exec 200>"$lockfile"
    if ! flock -n 200; then
        echo -ne "PLDM firmware update is already running, retry in 10 seconds, $retry_remain_count time(s) remaining...  "\\r
        retry_remain_count=$((retry_remain_count - 1))
        if [ $retry_remain_count -eq 0 ]; then
            echo ""
			echo "Retry time exhausted, abort"
			add_result_sel "${FAILURE_MSG}"
            exit 1
        fi
        sleep 10
    else
        break
    fi
done

SIGNAL_UNKNOWN=128
SIGNAL_EXIT_INT=130   # SIGINT (Ctrl+C)
SIGNAL_EXIT_TERM=143  # SIGTERM
SIGNAL_EXIT_HUP=129   # SIGHUP
SIGNAL_EXIT_QUIT=131  # SIGQUIT
SIGNAL_EXIT_PIPE=141  # SIGPIPE

trap 'cleanup_on_signal INT' INT
trap 'cleanup_on_signal TERM' TERM
trap 'cleanup_on_signal HUP' HUP
trap 'cleanup_on_signal QUIT' QUIT
trap 'cleanup_on_signal PIPE' PIPE

cleanup_on_signal() {
	local sig="$1"
	echo "signal received: $sig; restarting pldmd in background and exiting."
	systemctl restart pldmd >/dev/null 2>&1 &
	echo "NOTICE: pldmd is restarting. Please wait about 3 minutes before retrying the update."

	case "$sig" in
	INT)  exit $SIGNAL_EXIT_INT  ;;  # 130
	TERM) exit $SIGNAL_EXIT_TERM ;;  # 143
	HUP)  exit $SIGNAL_EXIT_HUP  ;;  # 129
	QUIT) exit $SIGNAL_EXIT_QUIT ;;  # 131
	PIPE) exit $SIGNAL_EXIT_PIPE ;;  # 141
	*)    exit $SIGNAL_UNKNOWN ;; #128
	esac
}


SB_CPLD_ADDR="0x22"
SB_UART_MUX_SWITCH_REG="0x08"
BIC_BOOT_STRAP_SPI_VAL="0x00"
BIC_BOOT_STRAP_REG="0x0C"

SD_EEPROM_ADDR="0x54"
S3_REMAINING_WRTIE_OFFSET=("0x09" "0x00")
CPU0_REMAINING_WRTIE_OFFSET=("0x09" "0x02")
CPU1_REMAINING_WRTIE_OFFSET=("0x09" "0x04")

RAA229620_RAA229621_RW_TIMES=28
MP2857_TPS536C5_RW_TIMES=1000

VR_TYPE=""
VR_OFFSET=""
VR_HIGHBYTE_OFFSET=""
VR_LOWBYTE_OFFSET=""
remaining_write_times=""

readonly SUCCESS_MSG="Success"
readonly FAILURE_MSG="Failure"

# For firmware update
# PLDM update
FAIL_TO_UPDATE_PLDM_TIME_OUT_ERROR=22
FAIL_TO_UPDATE_PLDM_FAIL_TO_DELETE_SOFTWARE_ID=23
FAIL_TO_UPDATE_PLDM_IMAGE_DOES_NOT_MATCH=24
FAIL_TO_UPDATE_PLDM_MISS_SOFTWARE_ID=25
FAIL_TO_UPDATE_PLDM_FAIL_TO_SET_REQUESTED_ACTIVATION=26
FAIL_TO_UPDATE_PLDM_FAIL_TO_INIT_REAMAINING_WRITE_UNKNOWN_VR_DEVICE=28
FAIL_TO_UPDATE_PLDM_SD_VR_SLOT_ID_IS_WRONG=30
FAIL_TO_UPDATE_PLDM_UNABLE_TO_DETERMINE_RETIMER_TYPE=33
NO_RETIMER_ON_THE_FRU=34
FAIL_TO_UPDATE_PLDM_PLDM_SOFTWARE_ACTIVATION_STATUS_FAIL=35
FAIL_TO_UPDATE_PLDM_MCTP_EID_NOT_EXIST=36


MANAGEMENT_BOARD_IO_EXP_BUS_NUM="34"
MANAGEMENT_BOARD_VERSION=$(get_product_version Management_Board)
if [ "$MANAGEMENT_BOARD_VERSION" = "DVT" ] || [ "$MANAGEMENT_BOARD_VERSION" = "EVT" ]; then
	MANAGEMENT_BOARD_IO_EXP_BUS_NUM="13"
fi

is_nuvoton_board="$(check_nuvoton_board)"

show_usage() {
	echo "Usage: pldm-fw-update.sh [sd|wf|sd_vr|wf_vr|sd_retimer] (--rcvy <slot_id> <uart image>) (<slot_id>) <pldm image>"
	echo "       update all PLDM component  : pldm-fw-update.sh [sd|wf] <pldm image>"
	echo "       update one PLDM component  : pldm-fw-update.sh [sd|wf|sd_vr|wf_vr|sd_retimer] <slot_id> <pldm image>"
	echo "       recover and then update one BIC(old method, NOT recommended) : pldm-fw-update.sh [sd|wf] --rcvy <slot_id> <uart image> <pldm image>"
	echo "       recover and then update one BIC by uart(new method, recommended): pldm-fw-update.sh [sd|wf] --rcvy <slot_id> <bic image>"
	echo "       recovery SD re-timer : pldm-fw-update.sh [sd_retimer] <slot_id> <pldm recovery image>"
	echo ""
}

check_power_on() {
	local slot=$1
	local i2c_bus=$((slot-1))
	local power_status=0

	power_status=$(gpioget "$(basename /sys/bus/i2c/devices/"$i2c_bus"-0023/*gpiochip*)" 16)
	if [ "$power_status" != "1" ]; then
		echo "Check Power : Off"
		echo "Power on before WF PLDM component update"
		exit 255
	fi

	echo "Check Power : On"
}

PLDM_FW_IDENT_PASS=0
PLDM_FW_IDENT_PASS_WITH_BACKWARD_COMPATIBILITY=1
PLDM_FW_IDENT_FAIL=255

retimer_get_package_source() {
	local input=$1
	case "$input" in
		"Vntg")
			echo "BRCM"
			;;
		*)
			echo "Astera"
			;;
	esac
}

retimer_get_source() {
	local slot=$1
	local target_bus=$((slot+15))

	result=$(busctl call xyz.openbmc_project.ObjectMapper \
		/xyz/openbmc_project/object_mapper \
		xyz.openbmc_project.ObjectMapper \
		GetSubTree sias "/xyz/openbmc_project/FruDevice" \
		0 1 "xyz.openbmc_project.FruDevice") || {
		echo "Failed to execute busctl call ObjectMapper"
		add_result_sel "${FAILURE_MSG}"
		exit 1
	}

	paths=$(echo "$result" | grep -o "/xyz/openbmc_project/FruDevice/Sentinel_Dome_Retimer_[^ ]*" | tr -d '"')

	for path in $paths; do
		bus=$(busctl get-property xyz.openbmc_project.FruDevice "$path" xyz.openbmc_project.FruDevice BUS 2>/dev/null | awk '{print $2}')
		if [ "$bus" == "$target_bus" ]; then
			board=$(busctl get-property xyz.openbmc_project.FruDevice "$path" xyz.openbmc_project.FruDevice BOARD_PRODUCT_NAME 2>/dev/null | sed -E 's/^s "//; s/"$//')
			vendor=$(echo "$board" | sed -E 's/.*-//')
			echo "$vendor"
			exit 0
		fi
	done

	echo "Cannot found corresponding vendor of bus $target_bus"
	exit 1
}

update_vr_new_bic_name() {
	local org_bic_name=${bic_name}
	local vr_type=$1

	if [ "${org_bic_name}" == "sd_vr" ]; then
		case "$vr_type" in
			*"$SD_S3_VR_ID")
				NEW_BIC_NAME="SD VR PVDD11_S3"
				;;
			*"$SD_CPU0_VR_ID")
				NEW_BIC_NAME="SD VR PVDDCR_CPU0"
				;;
			*"$SD_CPU1_VR_ID")
				NEW_BIC_NAME="SD VR PVDDCR_CPU1"
				;;
			*)
				echo "Failed to set the new bic name because of unknown VR type"
				;;
		esac
	elif [ "${org_bic_name}" == "wf_vr" ]; then
		case "$vr_type" in
			*"$WF_VR_INF_AB_CXL1")
				NEW_BIC_NAME="WF VR PVDDQ_AB 1"
				;;
			*"$WF_VR_INF_AB_CXL2")
				NEW_BIC_NAME="WF VR PVDDQ_AB 2"
				;;
			*"$WF_VR_INF_CD_CXL1")
				NEW_BIC_NAME="WF VR PVDDQ_CD 1"
				;;
			*"$WF_VR_INF_CD_CXL2")
				NEW_BIC_NAME="WF VR PVDDQ_CD 2"
				;;
			*)
				echo "Failed to set the new bic name because of unknown VR type"
				;;
		esac
	fi
}

sd_vr_get_source() {
	local slot=$1
	ret=$(busctl get-property -l xyz.openbmc_project.Settings /xyz/openbmc_project/software/host"$slot"/Sentinel_Dome_vr_pvdd11_s3 xyz.openbmc_project.Software.Version Version)
	case "$ret" in
		*"MPS"*)
			echo "MPS"
			;;
		*"Renesas"*)
			echo "RNS"
			;;
		*"Infineon"*)
			echo "INF"
			;;
		*"Texas Instruments"*)
			echo "TI"
			;;
		*)
			echo "Unknown"
			;;
	esac
}

wf_vr_get_source() {
	local slot=$1
	ret=$(busctl get-property -l xyz.openbmc_project.Settings /xyz/openbmc_project/software/host"$slot"/Wailua_Falls_vr_pvddq_ab_asic1 xyz.openbmc_project.Software.Version Version)
	case "$ret" in
		*"MPS"*)
			echo "MPS"
			;;
		*"Infineon"*)
			echo "INF"
			;;
		*)
			echo "Unknown"
			;;
	esac
}

vr_get_package_source() {
	local input=$1
	case "$input" in
		*"mp2857"*|*"mp2856"*|*"mp2971"*)
			echo "MPS"
			;;
		*"raa229620"*|*"raa229621"*)
			echo "RNS"
			;;
		*"tps536c5"*)
			echo "TI"
			;;
		*"xdpe12284c"*)
			echo "INF"
			;;
		*)
			echo "Unknown"
			;;
	esac
}

pldm_fw_identify() {
	local instance_type=$1
	local pldm_image=$2
	local slot_id=$3
	if [ "$instance_type" == "sd_retimer" ]; then
		comp_str="com.meta.Hardware.Yosemite4.SentinelDome.Retimer"
	elif [ "$instance_type" == "sd_vr" ]; then
		comp_str="com.meta.Hardware.Yosemite4.SentinelDome.VR"
	elif [ "$instance_type" == "wf_vr" ]; then
		comp_str="com.meta.Hardware.Yosemite4.WailuaFalls.VR"
	elif [ "$instance_type" == "sd" ]; then
		comp_str="com.meta.Hardware.Yosemite4.SentinelDome.BIC"
	elif [ "$instance_type" == "wf" ]; then
		comp_str="com.meta.Hardware.Yosemite4.WailuaFalls.BIC"
	else
		echo "Unknown instance type"
		return $PLDM_FW_IDENT_FAIL
	fi

	# Check if the PLDM image is valid
	if [ ! -f "$pldm_image" ]; then
		echo "PLDM image not found: $pldm_image"
		return $PLDM_FW_IDENT_FAIL
	fi

	# Parse PLDM Header
	
	local header
	header=$(hexdump -v -n 16 -s 0 -e '1/1 "%02X"' "$pldm_image")
	if [ "$header" != "F018878CCB7D49439800A02F059ACA02" ]; then
		echo "Invalid PLDM image header"
		return $PLDM_FW_IDENT_FAIL
	fi

	local offset=0x23
	local init_header_offset
	init_header_offset=$(hexdump -v -n 1 -s "$offset" -e '1/1 "%d"' "$pldm_image")
	((offset+=init_header_offset+4))
	local descriptor_count
	descriptor_count=$(hexdump -v -n 1 -s "$offset" -e '1/1 "%d"' "$pldm_image")
	((offset+=6))
	local comp_set_string_len
	comp_set_string_len=$(hexdump -v -n 1 -s "$offset" -e '1/1 "%d"' "$pldm_image")
	((offset+=4+comp_set_string_len))
	for i in $(seq 0 $((descriptor_count - 1))); do
		local descriptor_type
		descriptor_type=$(hexdump -v -n 2 -s "$offset" -e '1/2 "%u"' "$pldm_image")
		((offset+=2))
		local descriptor_length
		descriptor_length=$(hexdump -v -n 2 -s "$offset" -e '1/2 "%d"' "$pldm_image")
		((offset+=2))
		# if descriptor_type is not 0xFFFF, then skip
		if [ "$descriptor_type" -ne $((0xFFFF)) ]; then
			if [ "$descriptor_type" -eq $((0x0100)) ]; then
				pci_id=$(hexdump -v -n 2 -s "$offset" -e '1/1 "%02X"' "$pldm_image")
			fi
			((offset+=descriptor_length))
			continue
		fi
		# if descriptor_type is 0xFFFF, then it's vendor defined descriptor, find title
		# and check if it matches "OpenBMC.CompatibleHardware"
		# if it matches, then check if the data include
		# the component string
		# if it doesn't match, then skip

		# skip string type 1 byte
		((offset+=1))
		local descriptor_title_len
		descriptor_title_len=$(hexdump -v -n 1 -s "$offset" -e '1/1 "%d"' "$pldm_image")
		((offset+=1))
		local descriptor_title
		descriptor_title=$(hexdump -v -n "$descriptor_title_len" -s "$offset" -e '1/1 "%c"' "$pldm_image")
		((offset+=descriptor_title_len))    
		local descriptor_data_len=$((descriptor_length-descriptor_title_len-2))
		if [ "$descriptor_title" != "OpenBMC.CompatibleHardware" ]; then
			# skip descriptor data
			((offset+=descriptor_data_len))
			continue
		fi
		# if descriptor_title is "OpenBMC.CompatibleHardware", then check if the data include
		# the component string
		# if it doesn't match, then return error
		# if it matches, then return success
		local descriptor_data
		descriptor_data=$(hexdump -v -n "$descriptor_data_len" -s "$offset" -e '1/1 "%c"' "$pldm_image")
		if [[ "$descriptor_data" == *"$comp_str"* ]]; then
			return $PLDM_FW_IDENT_PASS
		else
			echo "This image is not compatible with $instance_type"
			return $PLDM_FW_IDENT_FAIL
		fi
	done

	((offset+=4))
	component_identifier=$(hexdump -v -n 2 -s "$offset" -e '1/2 "%u"' "$pldm_image")
	((offset+=19))
	component_string_len=$(hexdump -v -n 1 -s "$offset" -e '1/1 "%d"' "$pldm_image")
	((offset+=1))
	component_string=$(hexdump -v -n "$component_string_len" -s "$offset" -e '1/1 "%c"' "$pldm_image")
	((offset+=component_string_len+4))
	signature=$(hexdump -C -v -n 4 -s "$offset" "$pldm_image" | head -n1 | cut -d'|' -f2)
	
	if [ "$instance_type" == "sd_retimer" ]; then
		package_source=$(retimer_get_package_source "$signature")
		device_source=$(retimer_get_source "$slot_id")
		echo "package_source = $package_source"
		echo "device_source = $device_source"
		if [ "$package_source" != "$device_source" ]; then
			echo "This image is not compatible with $instance_type"
			return $PLDM_FW_IDENT_FAIL
		fi
		if [ "$component_identifier" -lt 4 ] || [ "$component_identifier" -gt 8 ] || [ "$component_identifier" -eq 6 ]; then
			echo "This image is not compatible with $instance_type"
			return $PLDM_FW_IDENT_FAIL
		fi
		if [ "$pci_id" != "0000" ]; then
			echo "This image is not compatible with $instance_type"
			return $PLDM_FW_IDENT_FAIL
		fi
	elif [ "$instance_type" == "sd_vr" ]; then
		package_source=$(vr_get_package_source "$component_string")
		device_source=$(sd_vr_get_source "$slot_id")
		echo "package_source = $package_source"
		echo "device_source = $device_source"
		if [ "$package_source" != "$device_source" ]; then
			echo "This image is not compatible with $instance_type"
			return $PLDM_FW_IDENT_FAIL
		fi
		if [ "$component_identifier" -lt 1 ] || [ "$component_identifier" -gt 3 ]; then
			echo "This image is not compatible with $instance_type"
			return $PLDM_FW_IDENT_FAIL
		fi
		if [ "$pci_id" != "0000" ]; then
			echo "This image is not compatible with $instance_type"
			return $PLDM_FW_IDENT_FAIL
		fi
	elif [ "$instance_type" == "wf_vr" ]; then
		package_source=$(vr_get_package_source "$component_string")
		device_source=$(wf_vr_get_source "$slot_id")
		echo "package_source = $package_source"
		echo "device_source = $device_source"
		if [ "$package_source" != "$device_source" ]; then
			echo "This image is not compatible with $instance_type"
			return $PLDM_FW_IDENT_FAIL
		fi
		if [ "$component_identifier" -lt 1 ] || [ "$component_identifier" -gt 4 ]; then
			echo "This image is not compatible with $instance_type"
			return $PLDM_FW_IDENT_FAIL
		fi
		if [ "$pci_id" != "0002" ]; then
			echo "This image is not compatible with $instance_type"
			return $PLDM_FW_IDENT_FAIL
		fi
	elif [ "$instance_type" == "sd" ]; then
		if [ "$component_identifier" -ne 0 ]; then
			echo "This image is not compatible with $instance_type"
			return $PLDM_FW_IDENT_FAIL
		fi
		if [ "$pci_id" != "0000" ]; then
			echo "This image is not compatible with $instance_type"
			return $PLDM_FW_IDENT_FAIL
		fi
	elif [ "$instance_type" == "wf" ]; then
		if [ "$component_identifier" -ne 0 ]; then
			echo "This image is not compatible with $instance_type"
			return $PLDM_FW_IDENT_FAIL
		fi
		if [ "$pci_id" != "0002" ]; then
			echo "This image is not compatible with $instance_type"
			return $PLDM_FW_IDENT_FAIL
		fi
	else
		echo "Unknown instance type"
		return $PLDM_FW_IDENT_FAIL
	fi
	
	return $PLDM_FW_IDENT_PASS_WITH_BACKWARD_COMPATIBILITY
}

# Simply booting from UART still requires an update to bring the BIC to a normal state.
recovery_bic_by_uart() {
	local slot=$1
	local cpld_uart_routing=$2
	local boot_strap_reg=$3
	local uart_image=$4
	local uart_num=$((slot < 5 ? slot - 1 :slot))
	local i2c_bus=$((slot-1))
	local ret=255

	echo "UART is ttyS$uart_num"
	echo "i2c bus is $i2c_bus"

	# Switch UART to corresponding BIC
	i2ctransfer -f -y "$i2c_bus" w2@"$SB_CPLD_ADDR" "$SB_UART_MUX_SWITCH_REG" "$cpld_uart_routing"
	ret=$?
	if [ "$ret" -ne 0 ]; then
		return $ret
	fi

	# Set BIC boot from UART
	echo "Setting BIC boot from UART"
	i2ctransfer -f -y "$i2c_bus" w2@"$SB_CPLD_ADDR" "$BIC_BOOT_STRAP_REG" "$boot_strap_reg"
	ret=$?
	if [ "$ret" -ne 0 ]; then
		return $ret
	fi

	# Notify Nuvoton MGM CPLD to change baud rate to 115200
	if [ -n "$is_nuvoton_board" ]; then
		if [ "$slot" -ge 5 ] && [ "$slot" -le 8 ]; then
			set_val=$((0x1 << (slot-1)))
			i2ctransfer -f -y $MANAGEMENT_BOARD_IO_EXP_BUS_NUM w2@0x21 0x0C "$set_val"
		fi
	fi

	# set UART to 115200
	/bin/stty -F "/dev/ttyS$uart_num" 115200
	ret=$?
	if [ "$ret" -ne 0 ]; then
		return $ret
	fi

	sleep 3

	echo "Doing the recovery update..."

	#echo "$uart_image"
	cat "$uart_image" > "/dev/ttyS$uart_num"

	sleep 5

	# Notify Nuvoton MGM CPLD to change baud rate to 57600
	if [ -n "$is_nuvoton_board" ]; then
		if [ "$slot" -ge 5 ] && [ "$slot" -le 8 ]; then
			i2ctransfer -f -y $MANAGEMENT_BOARD_IO_EXP_BUS_NUM w2@0x21 0x0C 0x0
		fi
	fi

	# set UART back to 57600
	/bin/stty -F "/dev/ttyS$uart_num" 57600
	ret=$?
	if [ "$ret" -ne 0 ]; then
		return $ret
	fi

	# Set BIC boot from spi
	i2ctransfer -f -y "$i2c_bus" w2@"$SB_CPLD_ADDR" "$BIC_BOOT_STRAP_REG" "$BIC_BOOT_STRAP_SPI_VAL"
	ret=$?
	if [ "$ret" -ne 0 ]; then
		return $ret
	fi

	echo "Recovery BIC is finished."
	return 0
}
recover_and_update_bic_by_uart() {
	local update_ret=0
	local slot=$1
	local cpld_uart_routing=$2
	local boot_strap_reg=$3
	local bic_image=$4
	local uart_num=$((slot < 5 ? slot - 1 :slot))
	local i2c_bus=$((slot-1))
	local ret=255
	echo "UART is ttyS$uart_num"
	echo "i2c bus is $i2c_bus"
	# Switch UART to corresponding BIC
	i2ctransfer -f -y "$i2c_bus" w2@"$SB_CPLD_ADDR" "$SB_UART_MUX_SWITCH_REG" "$cpld_uart_routing"
	ret=$?
	if [ "$ret" -ne 0 ]; then
		return $ret
	fi
	# Set BIC boot from UART
	echo "Setting BIC boot from UART"
	i2ctransfer -f -y "$i2c_bus" w2@"$SB_CPLD_ADDR" "$BIC_BOOT_STRAP_REG" "$boot_strap_reg"
	ret=$?
	if [ "$ret" -ne 0 ]; then
		return $ret
	fi
	# Notify Nuvoton MGM CPLD to change baud rate to 115200
	if [ -n "$is_nuvoton_board" ]; then
		if [ "$slot" -ge 5 ] && [ "$slot" -le 8 ]; then
			set_val=$((0x1 << (slot-1)))
			i2ctransfer -f -y $MANAGEMENT_BOARD_IO_EXP_BUS_NUM w2@0x21 0x0C "$set_val"
		fi
	fi

	# set UART to 115200
	/bin/stty -F "/dev/ttyS$uart_num" raw -echo 115200
	ret=$?
	if [ "$ret" -ne 0 ]; then
		return $ret
	fi

	# Stop the obmc-console service to avoid multiple access to the UART.
	systemctl stop obmc-console@ttyS$uart_num.service
	sleep 3
	
    # Recover and update the BIC firmware via UART using the Aspeed tool.
	(sleep 10; printf '\n') | timeout 120s /usr/bin/uart_fw_py_arm32 \
    --platform ast1030 \
    --spi 0 \
    --cs 0 \
    --comport /dev/ttyS$uart_num \
	--baudrate 115200   \
    --input_image "$bic_image"
	update_ret=$?
	sleep 5

	# Notify Nuvoton MGM CPLD to change baud rate to 57600
	if [ -n "$is_nuvoton_board" ]; then
		if [ "$slot" -ge 5 ] && [ "$slot" -le 8 ]; then
			i2ctransfer -f -y $MANAGEMENT_BOARD_IO_EXP_BUS_NUM w2@0x21 0x0C 0x0
		fi
	fi

	# set UART back to 57600
	/bin/stty -F "/dev/ttyS$uart_num" 57600
	ret=$?
	if [ "$ret" -ne 0 ]; then
		return $ret
	fi

	# Set BIC boot from spi
	i2ctransfer -f -y "$i2c_bus" w2@"$SB_CPLD_ADDR" "$BIC_BOOT_STRAP_REG" "$BIC_BOOT_STRAP_SPI_VAL"
	ret=$?
	if [ "$ret" -ne 0 ]; then
		return $ret
	fi
	echo "Restart MCTP service and PLDM service"
	sleep 3
	systemctl restart mctpd
	sleep 20
	systemctl restart pldmd
	sleep 60
	# Restart the obmc-console service
	systemctl restart obmc-console@ttyS$uart_num.service
	sleep 3

	echo "Slot$slot: Do 12V cycle"
	if ! busctl set-property xyz.openbmc_project.State.Chassis"$slot" /xyz/openbmc_project/state/chassis"$slot" xyz.openbmc_project.State.Chassis RequestedPowerTransition s "xyz.openbmc_project.State.Chassis.Transition.PowerCycle" --timeout=120 2>&1; then
		echo "Failed to do 12V cycle"
	fi

	sleep 40
	if [ "$update_ret" -ne 0 ]; then
		echo "UART firmware tool failed: $update_ret"
		return "$update_ret"
	fi
	# Update BIC version to Settings D-Bus
	update_bic_version_dbus "$bic_name" "$slot_id"
	echo "Update BIC via UART is finished."
	return 0
}
wait_for_update_complete() {
	local counter=5
	local output status progress ret
	while true
	do
		sleep 5
		echo -ne "Waiting for updating... ${counter} sec"\\r

		output=$(busctl get-property xyz.openbmc_project.PLDM /xyz/openbmc_project/software/"$software_id" xyz.openbmc_project.Software.Activation Activation --timeout=120 2>&1)
		ret=$?
		if [ "$ret" -eq 0 ]; then
			status=$(echo "$output" | cut -d " " -f 2)
			if [ "${status}" == "\"xyz.openbmc_project.Software.Activation.Activations.Failed\"" ] || [ "${status}" == "\"xyz.openbmc_project.Software.Activation.Activations.Invalid\"" ]; then
				echo -ne \\n"PLDM software activation is ${status}."\\n
				return $FAIL_TO_UPDATE_PLDM_PLDM_SOFTWARE_ACTIVATION_STATUS_FAIL
			fi
			if [ "${status}" == "\"xyz.openbmc_project.Software.Activation.Activations.Active\"" ]; then
				output=$(busctl get-property xyz.openbmc_project.PLDM /xyz/openbmc_project/software/"$software_id" xyz.openbmc_project.Software.ActivationProgress Progress --timeout=120 2>&1)
				ret=$?
				if [ "$ret" -eq 0 ]; then
					progress=$(echo "$output" | cut -d " " -f 2)
					if [ "${progress}" == 100 ]; then
						echo -ne \\n"Update done."\\n
						return 0
					fi
				else
					echo -e "\nFailed to read update progress: $output"
				fi
			fi
		else
			echo -e "\nFailed to read software activation: $output"
		fi
		if echo "$output" | grep -q "Unknown object"; then
			echo "The software ID $software_id does not exist."
			return $FAIL_TO_UPDATE_PLDM_MISS_SOFTWARE_ID
		fi

		counter=$((counter+5))
		# Over five minutes is considered timeout
		if [ "${counter}" == 300 ]; then
			echo -ne \\n"Time out. Fail"\\n
			return $FAIL_TO_UPDATE_PLDM_TIME_OUT_ERROR
		fi
	done
}

delete_software_id() {
	local counter=0
	if [ "$software_id" != "" ]; then
		while true
		do
			sleep 2
			activation=$(busctl  get-property xyz.openbmc_project.PLDM /xyz/openbmc_project/software/"$software_id" xyz.openbmc_project.Software.Activation Activation --timeout=120 2>&1 | cut -d " " -f 2)
			#Check the status of pldm updater, if the status is in activating, we cannot call the delete method
			if [ "${activation}" != "\"xyz.openbmc_project.Software.Activation.Activations.Activating\"" ]; then
				echo "Delete software id. software id = $software_id"
				output=$(busctl call xyz.openbmc_project.PLDM /xyz/openbmc_project/software/"$software_id" xyz.openbmc_project.Object.Delete Delete --timeout=120 2>&1)
				local ret=$?
				if [ "$ret" -ne 0 ]; then
					echo "Failed to delete software id. Return with error code: $FAIL_TO_UPDATE_PLDM_FAIL_TO_DELETE_SOFTWARE_ID. Error message: $output"
					return $FAIL_TO_UPDATE_PLDM_FAIL_TO_DELETE_SOFTWARE_ID
				fi
				break
			fi
			counter=$((counter+2))
			if [ "${counter}" == 10 ]; then
				echo "Object /xyz/openbmc_project/software/$software_id is stalled in activating"
				echo "Restarting pldmd to recover it."
				systemctl stop pldmd
				sleep 10
				systemctl start pldmd
				sleep 60
				return $FAIL_TO_UPDATE_PLDM_FAIL_TO_DELETE_SOFTWARE_ID
			fi
		done
	fi
}
update_bic_version_dbus() {
	local bic_name=$1
	local slot_id=$2
	if [ "$bic_name" == "sd" ]; then
	echo "Updating SD BIC version to Settings D-Bus"
	sleep 15 # wait for BIC reset
	# Check and reset unresponsive slots in same hub before version check
	check_and_reset_unresponsive_slots_in_same_hub "$slot_id"
	# Check if slot_id is empty
	if [ -z "$slot_id" ]; then
		# Loop through slot_id values from 1 to 8 if slot_id is empty
		for slot_id in {1..8}; do
			# Check EID presence first
			echo "$mctp_tree" | grep -q "/au/com/codeconstruct/mctp1/networks/1/endpoints/${slot_id}0"
			ret=$?
			if [ "$ret" -eq 0 ]; then
				/usr/libexec/fw-versions/sd-bic "$slot_id"
				ret=$?
				# Check if the command was successful
				if [ "$ret" -eq 0 ]; then
					version=$(busctl get-property xyz.openbmc_project.Settings \
						"/xyz/openbmc_project/software/host$slot_id/Sentinel_Dome_bic" \
						xyz.openbmc_project.Software.Version Version --timeout=120 2>&1 | awk -F'"' '{print $2}')
					echo "Version retrieved successfully: $version"
				fi
			fi
		done
	else
		# Execute the command with the provided slot_id
		/usr/libexec/fw-versions/sd-bic "$slot_id"
		ret=$?
		# Check if the command was successful
		if [ "$ret" -eq 0 ]; then
			version=$(busctl get-property xyz.openbmc_project.Settings \
				"/xyz/openbmc_project/software/host$slot_id/Sentinel_Dome_bic" \
				xyz.openbmc_project.Software.Version Version --timeout=120 2>&1 | awk -F'"' '{print $2}')
			echo "Version retrieved successfully: $version"
		fi
	fi
	elif [ "$bic_name" == "wf" ]; then
		echo "Updating WF BIC version to Settings D-Bus"
		sleep 15 # wait for BIC reset
		# Check and reset unresponsive slots in same hub before version check
		check_and_reset_unresponsive_slots_in_same_hub "$slot_id"
		# Check if slot_id is empty
		if [ -z "$slot_id" ]; then
			# Loop through slot_id values from 1 to 8 if slot_id is empty
			for slot_id in {1..8}; do
				# Check EID presence first
				echo "$mctp_tree" | grep -q "/au/com/codeconstruct/mctp1/networks/1/endpoints/${slot_id}2"
				ret=$?
				if [ "$ret" -eq 0 ]; then
					/usr/libexec/fw-versions/wf-bic "$slot_id"
					ret=$?
					# Check if the command was successful
					if [ "$ret" -eq 0 ]; then
						version=$(busctl get-property xyz.openbmc_project.Settings \
							"/xyz/openbmc_project/software/host$slot_id/Wailua_Falls_bic" \
							xyz.openbmc_project.Software.Version Version --timeout=120 2>&1 | awk -F'"' '{print $2}')
						echo "Version retrieved successfully: $version"
					fi
				fi
			done
		else
			# Execute the command with the provided slot_id
			/usr/libexec/fw-versions/wf-bic "$slot_id"
			ret=$?
			# Check if the command was successful
			if [ "$ret" -eq 0 ]; then
				version=$(busctl get-property xyz.openbmc_project.Settings \
					"/xyz/openbmc_project/software/host$slot_id/Wailua_Falls_bic" \
					xyz.openbmc_project.Software.Version Version --timeout=120 2>&1 | awk -F'"' '{print $2}')
				echo "Version retrieved successfully: $version"
			fi
		fi
	fi
}


update_bic() {
	cp "$1" /tmp/pldm_images
	sleep 6

	echo "Generating software ID..."
	software_id=$(busctl tree xyz.openbmc_project.PLDM | grep "/xyz/openbmc_project/software/[0-9]\+" | awk -F'/' '{print $5}' | head -n1)
	echo "software_id = $software_id"

	if [ "$software_id" != "" ]; then
		if ! busctl get-property xyz.openbmc_project.PLDM /xyz/openbmc_project/software/"$software_id" xyz.openbmc_project.Software.ActivationProgress Progress --timeout=120 >/dev/null 2>&1; then
			echo "The image does not match with any devices. Return with error code: $FAIL_TO_UPDATE_PLDM_IMAGE_DOES_NOT_MATCH"
			return $FAIL_TO_UPDATE_PLDM_IMAGE_DOES_NOT_MATCH
		fi
		sleep 10
		if ! busctl set-property xyz.openbmc_project.PLDM /xyz/openbmc_project/software/"$software_id" xyz.openbmc_project.Software.Activation RequestedActivation s "xyz.openbmc_project.Software.Activation.RequestedActivations.Active" --timeout=120 2>&1; then
			echo "Failed to set RequestedActivation. Return with error code: $FAIL_TO_UPDATE_PLDM_FAIL_TO_SET_REQUESTED_ACTIVATION"
			return $FAIL_TO_UPDATE_PLDM_FAIL_TO_SET_REQUESTED_ACTIVATION
		fi
		wait_for_update_complete
		update_status=$?
		if [ "$3" == "wf" ] || [ "$3" == "sd" ]; then
			bic_eid="${2}0"
			if [ "$3" == "wf" ]; then
				bic_eid=$((bic_eid+2))
			fi
			if [ $update_status == $FAIL_TO_UPDATE_PLDM_TIME_OUT_ERROR ]; then
				local output
				output=$(pldmtool fw_update GetStatus -m "${bic_eid}")
				bic_current_state=$(echo "$output" | grep -o '"CurrentState": "[^"]*' | cut -d'"' -f4)
				if [ "$bic_current_state" = "DOWNLOAD" ]; then
				pldmtool raw -d 0x80 0x3f 0x1 0x15 0xa0 0x0 0x18 0x03 -m "${bic_eid}"
				sleep 6 # wait for BIC reset
				fi
			fi
		fi
		return $update_status
	else
		echo "Fail: Miss software id."
		return $FAIL_TO_UPDATE_PLDM_MISS_SOFTWARE_ID
	fi
}


update_all_slots() {
	local is_recovery=$1
	local user_specific_slot_id=$2

	if [ "$is_recovery" == false ] && [ -z "$user_specific_slot_id" ]; then
		# 0 = true
		return 0
	else
		return 1
	fi
}

update_single_slot() {
	local slot_id=$1
	local user_specific_slot_id=$2
	local is_recovery=$3

	if [ "$is_recovery" == false ] && [ "$slot_id" == "$user_specific_slot_id" ]; then
		# 0 = true
		return 0
	else
		return 1
	fi
}

conduct_normal_update_and_updated_slot() {
	local slot_id=$1
	local user_specific_slot_id=$2
	local is_recovery=$3

	if update_single_slot "$slot_id" "$user_specific_slot_id" "$is_recovery" ; then
		# 0 = true
		return 0
	elif update_all_slots "$is_recovery" "$user_specific_slot_id"; then
		return 0
	else
		return 1
	fi
}

conduct_recovery_and_not_recovered_slot() {
	local slot_id=$1
	local user_specific_slot_id=$2
	local is_recovery=$3

	if [ "$is_recovery" == true ] && [ "$slot_id" != "$user_specific_slot_id" ]; then
		# 0 = true
		return 0
	else
		return 1
	fi
}

is_sd_bic() {
	local slot_id=$1
	local mctp_exp_id=$2

	if [[ "$slot_id" -ge 1 && "$slot_id" -le 8 && "$mctp_exp_id" == "0" ]]; then
		# 0 = true
		return 0
	else
		return 1
	fi
}


is_other_bic_updating() {
  echo "Check if other PLDM components are updating"

  retry_remain_count=$RETRY_UPDATE_COUNT
  while true
  do
	  pldm_output=$(busctl tree xyz.openbmc_project.PLDM --timeout=120)
	  if echo "$pldm_output" | grep -qE "/xyz/openbmc_project/software/[0-9]+"; then
		echo "$pldm_output" | grep -E "/xyz/openbmc_project/software/[0-9]+"
		previous_software_id=$(echo "$pldm_output" | grep /xyz/openbmc_project/software/ | cut -d "/" -f 5)
		busctl get-property xyz.openbmc_project.PLDM /xyz/openbmc_project/software/"$previous_software_id" xyz.openbmc_project.Software.ActivationProgress Progress --timeout=120 > /dev/null 2>&1
		ret=$?
		if [ "$ret" -eq 0 ]; then
			echo -ne "Please wait until the other software update is completed, retry in 10 seconds, $retry_remain_count time(s) remaining... "\\r
			retry_remain_count=$((retry_remain_count - 1))
			if [ $retry_remain_count -eq 0 ]; then
				echo ""
				echo "Retry time exhausted, abort"
				add_result_sel "${FAILURE_MSG}"
				exit 255
			fi
		else
			break
		fi
	  else
		break
	  fi
	  sleep 10
  done
}

# Function to prompt for continuation and check user input
prompt_confirmation() {
	echo "WARNING! This will automatically update all PLDM components."
	read -r -p "Continue? [y/N] " continue
	if [ "$continue" != "y" ]; then
		echo "Aborting on user request."
		exit 0
	fi
}

# Function to display an error message and exit the script
error_and_exit() {
	echo "Please provide $1 image"
	show_usage
	add_result_sel "${FAILURE_MSG}"
	exit 255
}

get_offset_for_vr_type() {
    local vr_type=$1

    case "$vr_type" in
        *"$SD_S3_VR_ID")
            printf "%s\n" "${S3_REMAINING_WRTIE_OFFSET[@]}"
            ;;
        *"$SD_CPU0_VR_ID")
            printf "%s\n" "${CPU0_REMAINING_WRTIE_OFFSET[@]}"
            ;;
        *"$SD_CPU1_VR_ID")
            printf "%s\n" "${CPU1_REMAINING_WRTIE_OFFSET[@]}"
            ;;
        *)
            echo "Failed to initialize remaining write because of unknown VR type"
			add_result_sel "${FAILURE_MSG}"
            exit 255
            ;;
    esac
}

Initialize_vr_remaining_write() {
    local slot_id=$1
    local eeprom_addr=$2
    local pldm_image=$3
    local hex_dump

    hex_dump=$(hexdump -c "$pldm_image" | head -n 15 | cut -c 9- | tr -d '\n[:space:]')

    case "$hex_dump" in
        *"raa229620"*|*"raa229621"*)
            remaining_write_times=$RAA229620_RAA229621_RW_TIMES
            ;;
        *"mp2857"*|*"tps536c5"*|*"mp2856"*)
            remaining_write_times=$MP2857_TPS536C5_RW_TIMES
            ;;
        *)
            echo "Failed to initialize remaining write because of unknown VR device"
			add_result_sel "${FAILURE_MSG}"
            exit $FAIL_TO_UPDATE_PLDM_FAIL_TO_INIT_REAMAINING_WRITE_UNKNOWN_VR_DEVICE
            ;;
    esac

    set_vr_remaining_write_to_eeprom "$slot_id" "$eeprom_addr" "$remaining_write_times"
}

check_vr_remaining_write() {
    local i2c_bus=$(($1+15))
    local slot_id=$1
    local eeprom_addr=$2
    local pldm_image=$3
    local result

    i2ctransfer -f -y $i2c_bus w2@"$eeprom_addr" "$VR_HIGHBYTE_OFFSET" "$VR_LOWBYTE_OFFSET"

    result=$(i2ctransfer -f -y  $i2c_bus r2@"$eeprom_addr")

    IFS=' ' read -ra result_array <<< "$result"
    result="0x${result_array[0]#0x}${result_array[1]#0x}"
    remaining_write_times=$(printf "%d" "$result")

    if [ "$remaining_write_times" -eq 0 ]; then
        echo "VR device can't be updated because the remaining write times is 0"
		add_result_sel "${FAILURE_MSG}"
        exit 255
    fi
}

set_vr_remaining_write_to_eeprom() {
	local i2c_bus=$(($1+15))
	local eeprom_addr=$2
	local rw_times=$3
	local hex

	hex=$(printf "%04x" "$rw_times")
	# Split the hex into two parts
	local hex1=${hex:0:2}
	local hex2=${hex:2}
	# Add the prefix "0x" to each part
	local byte1="0x$hex1"
	local byte2="0x$hex2"
	# Write the remaining write times back to EEPROM
	i2ctransfer -f -y $i2c_bus w4@"$eeprom_addr" "$VR_HIGHBYTE_OFFSET" "$VR_LOWBYTE_OFFSET" "$byte1" "$byte2"
}

check_if_no_retimer_sku() {
    local slot_id=$1

    # retimer 1 CHIP GPIO 97
    gpio_97=$(pldmtool raw -d 0x80 0x02 0x3a 0x61 0xFF -m "${slot_id}0" 2>/dev/null)
    # echo "gpio_97 = $gpio_97"
    gpio_97_rx_data=$(echo "$gpio_97" | grep 'Rx:' | cut -d':' -f2-)
    # echo "gpio_97_rx_data = $gpio_97_rx_data"
    gpio_97_cc_byte=$(echo "$gpio_97_rx_data" | awk '{print $(NF-7)}')
    # echo "gpio_97_cc_byte = $gpio_97_cc_byte"
    gpio_97_last_byte=$(echo "$gpio_97_rx_data" | awk '{print $NF}' )
    # echo "gpio_97_last_byte = $gpio_97_last_byte"
    # retimer 0 CHIP GPIO 95
    gpio_95=$(pldmtool raw -d 0x80 0x02 0x3a 0x5f 0xFF -m "${slot_id}0" 2>/dev/null)
    # echo "gpio_95 = $gpio_95"
    gpio_95_rx_data=$(echo "$gpio_95" | grep 'Rx:' | cut -d':' -f2-)
    # echo "gpio_95_rx_data = $gpio_95_rx_data"
    gpio_95_cc_byte=$(echo "$gpio_95_rx_data" | awk '{print $(NF-7)}')
    # echo "gpio_95_cc_byte = $gpio_95_cc_byte"
    gpio_95_last_byte=$(echo "$gpio_95_rx_data" | awk '{print $NF}' )
    # echo "gpio_95_last_byte = $gpio_95_last_byte"

    # pldm tool raw command failed
    if [ "$gpio_97_cc_byte" != "00" ] || [ "$gpio_95_cc_byte" != "00" ]; then
        echo "Retimer update failed, unable to determine retimer type."
        return $FAIL_TO_UPDATE_PLDM_UNABLE_TO_DETERMINE_RETIMER_TYPE
    fi

    # 0102==retimer[1:0] get 01, means no retimer on the FRU.
    retimer_type="${gpio_97_last_byte}${gpio_95_last_byte}"
    # echo "retimer_type = $retimer_type"
    if [ "$retimer_type" == "0102" ]; then
        return $NO_RETIMER_ON_THE_FRU
    fi

    return 0
}

sd_bic_do_i3c_re_init () {
	# Workaround: Sentinel dome BIC do i3c hub re-init
	mapfile -t eid_arr < <(echo "$mctp_tree" | cut -d "/" -f 9)
	for EID in "${eid_arr[@]}"
	do
		mctp_slot_id=$((EID/10));
		mctp_exp_id=$((EID%10));

		if is_sd_bic "$mctp_slot_id" "$mctp_exp_id"; then
			if conduct_recovery_and_not_recovered_slot "$mctp_slot_id" "$slot_id" "$is_rcvy" ||
			conduct_normal_update_and_updated_slot "$mctp_slot_id" "$slot_id" "$is_rcvy"; then
				echo "Slot $mctp_slot_id: Restart BIC"
				pldmtool raw -m "$mctp_slot_id"0 -d 0x80 0x02 0x39 0x1 0x1 0x1 0x1 0x1
			fi
		fi
	done
	sleep 5
}


handle_firmware_operations () {
	# Execute recovery operations if in recovery mode, based on the value of bic_name
	echo "Start processing firmware operations for slot $slot_id using PLDM."
	if [ "$is_rcvy" == true ]; then

		is_other_bic_updating

		# Workaround: Avoid pldm daemon blocked
		systemctl stop pldmd
		echo "Start to Recovery slot $slot_id $bic_name BIC"
		case $bic_name in
		sd)
			recovery_bic_by_uart "$slot_id" "0x04" "0x01" "$uart_image"
			ret=$?
			;;
		wf)
			check_power_on "$slot_id"
			recovery_bic_by_uart "$slot_id" "0x01" "0x02" "$uart_image"
			ret=$?
			;;
		esac

		if [ "$ret" -ne 0 ]; then
			sleep 10
			systemctl start pldmd
			sleep 60
			echo "Failed to Recovery BIC. Exiting with error code: $ret"
			exit "$ret"
		fi

		echo "Restart MCTP service and PLDM service"
		sleep 3
		systemctl restart mctpd
		sleep 20
		systemctl start pldmd
		sleep 60
	fi

	# TODO: WF need to implement the remaining write times check
	if [ "$bic_name" == "sd_vr" ]; then
		if ! [[ "$slot_id" =~ ^[1-8]$ ]]; then
			echo "Failed to update SD VR because <slot${slot_id}> is wrong"
			add_result_sel "${FAILURE_MSG}"
			exit $FAIL_TO_UPDATE_PLDM_SD_VR_SLOT_ID_IS_WRONG
		fi
		check_vr_remaining_write "$slot_id" "$SD_EEPROM_ADDR" "$pldm_image"
		echo "remaining write before updating: $remaining_write_times"
	fi

	if ! systemctl is-active --quiet pldmd; then
		echo "STOP. PLDM service is not running. Please check pldmd status."
		add_result_sel "${FAILURE_MSG}"
		exit 255
	fi

	mctp_tree=$(busctl tree au.com.codeconstruct.MCTP1 --timeout=120 2>&1 | grep "/au/com/codeconstruct/mctp1/networks/1/endpoints")
	echo "$mctp_tree"
	case "$bic_name" in
	sd|sd_vr|sd_retimer)
		if [ "$is_rcvy" == false ] && ! echo "$mctp_tree" | grep -q "/au/com/codeconstruct/mctp1/networks/1/endpoints/${slot_id}0"; then
			echo "EID ${slot_id}0 does not exist, unable to update PLDM device. Return with error code: $FAIL_TO_UPDATE_PLDM_MCTP_EID_NOT_EXIST"
			return $FAIL_TO_UPDATE_PLDM_MCTP_EID_NOT_EXIST
		fi
		;;
	wf|wf_vr)
		if [ "$is_rcvy" == false ] && ! echo "$mctp_tree" | grep -q "/au/com/codeconstruct/mctp1/networks/1/endpoints/${slot_id}2"; then
			echo "EID ${slot_id}2 does not exist, unable to update PLDM device. Return with error code: $FAIL_TO_UPDATE_PLDM_MCTP_EID_NOT_EXIST"
			return $FAIL_TO_UPDATE_PLDM_MCTP_EID_NOT_EXIST
		fi
		;;
	esac
	echo "Start to Update PLDM component"

	is_other_bic_updating
	check_and_reset_unresponsive_slots_in_same_hub "$slot_id"
	update_bic "$pldm_image" "$slot_id" "$bic_name"
	ret=$?
	if [ "$ret" -ne 0 ]; then
		echo "Failed to Update PLDM component. Return with error code: $ret"
		delete_software_id
		check_and_reset_unresponsive_slots_in_same_hub "$slot_id"
		echo "Restart PLDM service for recover"
		systemctl stop pldmd
		sleep 10
		systemctl start pldmd
		sleep 60
		return $ret
	fi

	sleep 3

	if [ "$is_rcvy" == true ]; then
		echo "Slot$slot_id: Do 12V cycle"
		if ! busctl set-property xyz.openbmc_project.State.Chassis"$slot_id" /xyz/openbmc_project/state/chassis"$slot_id" xyz.openbmc_project.State.Chassis RequestedPowerTransition s "xyz.openbmc_project.State.Chassis.Transition.PowerCycle" --timeout=120 2>&1; then
			echo "Failed to do 12V cycle"
		fi
		sleep 40
	fi

	if [ "$bic_name" == "sd_vr" ]; then
		i2c_bus=$((slot_id+15))
		i2ctransfer -f -y $i2c_bus w2@"$SD_EEPROM_ADDR" "$VR_HIGHBYTE_OFFSET" "$VR_LOWBYTE_OFFSET"
		result=$(i2ctransfer -f -y  $i2c_bus r2@"$SD_EEPROM_ADDR")
		if [ "$result" == "0xff 0xff" ]; then
			Initialize_vr_remaining_write "$slot_id" "$SD_EEPROM_ADDR" "$pldm_image"
		fi
		remaining_write_times=$((remaining_write_times-1))
		set_vr_remaining_write_to_eeprom "$slot_id" "$SD_EEPROM_ADDR" "$remaining_write_times"
		echo "remaining write after updating: $remaining_write_times"
	fi

	delete_software_id
	# Update BIC version to Settings D-Bus
	update_bic_version_dbus "$bic_name" "$slot_id"
	add_result_sel "${SUCCESS_MSG}"
	echo "Completed firmware operations for slot $slot_id."
	echo "Done"
	return 0
}

retry_firmware_operation() {
    local retries=0
    local ret=0

    handle_firmware_operations
    ret=$?
    if [ $ret -eq 0 ]; then
        return 0
	elif [ $ret -eq $FAIL_TO_UPDATE_PLDM_MCTP_EID_NOT_EXIST ]; then
	    add_result_sel "${FAILURE_MSG}"
		return $ret
    fi

    while [ $retries -lt $MAX_RETRIES ]; do
        retries=$((retries + 1))
        echo "Firmware operation for slot $slot_id failed. Retrying... ($retries/$MAX_RETRIES)"
        handle_firmware_operations
        ret=$?
        if [ $ret -eq 0 ]; then
            return 0
        fi
    done

    add_result_sel "${FAILURE_MSG}"
    echo "Maximum retries ($MAX_RETRIES) reached. Exiting with error code: $ret."
    exit "$ret"
}

check_and_reset_unresponsive_slots_in_same_hub() {
	local current_slot=$1
	local start_slot=1
	local end_slot=4

	if [ "$current_slot" -ge 5 ] && [ "$current_slot" -le 8 ]; then
		start_slot=5
		end_slot=8
	fi

	for hub_slot in $(seq "$start_slot" "$end_slot"); do
		local eid
		eid="${hub_slot}0"
		hub_bus_id=$((hub_slot-1))

		if pldmtool base GetTID -m "$eid" 2>/dev/null | grep -q "\"Response\": $eid"; then
			echo "Slot $hub_slot (EID $eid): GetTID response OK"
		else
			echo "Slot $hub_slot (EID $eid): no valid GetTID response, do reset"
			i2ctransfer -f -y "$hub_bus_id" w2@0x21 0x8 0x0
			sleep 1
			i2ctransfer -f -y "$hub_bus_id" w2@0x21 0x8 0x1
			sleep 15
			
			# Check again after reset, retry up to 3 times
			local retry_count=0
			while [ $retry_count -lt 3 ]; do
				if pldmtool base GetTID -m "$eid" 2>/dev/null | grep -q "\"Response\": $eid"; then
					echo "Slot $hub_slot (EID $eid): GetTID response OK after retry $((retry_count+1))"
					break
				else
					echo "Slot $hub_slot (EID $eid): still no valid response, retry $((retry_count+1))/3"
					i2ctransfer -f -y "$hub_bus_id" w2@0x21 0x8 0x0
					sleep 1
					i2ctransfer -f -y "$hub_bus_id" w2@0x21 0x8 0x1
					sleep 15
					((retry_count++))
				fi
			done
		fi
	done
}

add_init_sel() {
	if [ "$is_rcvy" != true ]; then
		log-create ${TargetDetermined} --json "{\"TARGET_NAME\":\"${NEW_BIC_NAME} slot${slot_id}\", \"IMAGE_IDENTIFIER\":\"${pldm_image}\"}"
	fi
}

add_result_sel() {
	RESULT="$1"

	# NEW_BIC_NAME is only resolved after the image is identified, so an update
	# blocked by pldm_fw_identify() can still have it empty (VR targets).
	local target_name="${NEW_BIC_NAME:-$bic_name}"

	if [ "$is_rcvy" != true ]; then
	    if [ "${RESULT}" == "${FAILURE_MSG}" ]; then
		    log-create ${ActivateFailed} --json "{\"IMAGE_IDENTIFIER\":\"${pldm_image}\", \"TARGET_NAME\":\"${target_name} slot${slot_id}\"}"
	    elif [ "${RESULT}" == "${SUCCESS_MSG}" ]; then
		    log-create ${UpdateSuccessful} --json "{\"TARGET_NAME\":\"${target_name} slot${slot_id}\", \"IMAGE_IDENTIFIER\":\"${pldm_image}\"}"
	    fi
	fi
}

get_new_bic_name() {
	local bic_name="$1"
	case "$bic_name" in
		sd)
			echo "SD BIC"
			;;
		wf)
			echo "WF BIC"
			;;
		sd_retimer)
			echo "SD RETIMER"
			;;
		sd_vr)
			;;
		wf_vr)
			;;
		*)
			echo "$bic_name"
			;;
	esac
}

get_vr_type() {
	local init_header_offset descriptor_header_offset vr_hex

	init_header_offset=$(hexdump -n 1 -s 0x23 -e '1/1 "%d"' "$pldm_image") || return 1
	descriptor_header_offset=$(hexdump -n 2 -s $((0x23 + init_header_offset + 2)) -e '1/2 "%d"' "$pldm_image") || return 1
	vr_hex=$(hexdump -n 1 -s $((0x23 + init_header_offset + 2 + descriptor_header_offset + 4)) -e '1/1 "%02x"' "$pldm_image") || return 1

	echo "$vr_hex"
}

# Check for minimum required arguments
[ $# -lt 2 ] && error_and_exit "PLDM"

slot_id=
bic_name=$1
pldm_image=$2

NEW_BIC_NAME=$(get_new_bic_name "$bic_name")

# Determine recovery mode and check for required image files based on argument count
if [ $# -eq 5 ] && [ "$2" == "--rcvy" ]; then
	echo -e "[Warning]: You are recovering and updating one BIC using an outdated method. 
			This method is not recommended and will no longer be maintained.\n\n 
			It is recommended to run the following command:
		  	pldm-fw-update.sh [sd|wf] --rcvy <slot_id> <bic image>"
	if [ "$bic_name" == "sd_vr" ] || [ "$bic_name" == "wf_vr" ]; then
		echo "VR device can't be updated in recovery mode"
		exit 255
	elif [ "$bic_name" == "sd" ]; then
		echo "Initiate SD BIC recovery"
	elif [ "$bic_name" == "wf" ]; then
		echo "Initiate WF BIC recovery"
	fi

	slot_id=$3
	uart_image=$4
	[ ! -f "$uart_image" ] && error_and_exit "UART"
	pldm_image=$5
	pldm_fw_identify "$bic_name" "$pldm_image"
	ident_result=$?
	if [ $ident_result -eq $PLDM_FW_IDENT_FAIL ]; then
		exit 255
	fi
	pldm-package-re-wrapper bic -s "$slot_id" -f "$pldm_image"
	pldm_image="${pldm_image}_re_wrapped"
	echo "PLDM image name: ${pldm_image}"
	retry_firmware_operation
elif [ $# -eq 4 ] && [ "$2" == "--rcvy" ]; then
    if [ "$bic_name" == "sd_vr" ] || [ "$bic_name" == "wf_vr" ]; then
		echo "VR device can't be updated in recovery mode"
		exit 255
	elif [ "$bic_name" == "sd" ]; then
		echo "Initiate SD BIC recovery"
	elif [ "$bic_name" == "wf" ]; then
		echo "Initiate WF BIC recovery"
	fi
	use_uart_update=true

	slot_id=$3
	bic_image=$4

	#Aspeed tool requires ast1030_uart_fw.bin to be in the Work directory
	ln -s /usr/bin/ast1030_uart_fw.bin "$(pwd)/ast1030_uart_fw.bin"

	echo "Start recover and update  $slot_id $bic_name BIC via UART"
	ret=255
	case $bic_name in
	sd)
		recover_and_update_bic_by_uart "$slot_id" "0x04" "0x01" "$bic_image"
		ret=$?
		;;
	wf)
		check_power_on "$slot_id"
		recover_and_update_bic_by_uart "$slot_id" "0x01" "0x02" "$bic_image"
		ret=$?
		;;
	esac
	rm "$(pwd)/ast1030_uart_fw.bin"
	if [ "$ret" -ne 0 ]; then
		echo "Failed to recover and update BIC via UART. Return with error code: $ret"
		exit "$ret"
	fi
elif [ $# -eq 3 ] && [[ "$2" =~ ^[1-8]$ ]]; then
	slot_id=$2
	pldm_image=$3

	if [ "$bic_name" != "sd_vr" ] && [ "$bic_name" != "wf_vr" ]; then
		add_init_sel
	fi

	pldm_fw_identify "$bic_name" "$pldm_image" "$slot_id"
	ident_result=$?
	if [ $ident_result -eq $PLDM_FW_IDENT_FAIL ]; then
		add_result_sel "${FAILURE_MSG}"
		exit 255
	fi

	if [ "$bic_name" == "sd_vr" ]; then
		echo "Initiate SD VR update"
		if [ $ident_result -eq $PLDM_FW_IDENT_PASS ]; then
			pldm-package-re-wrapper ds -s "$slot_id" -f "$pldm_image"
			pldm_image="${pldm_image}_re_wrapped"
		elif [ $ident_result -eq $PLDM_FW_IDENT_PASS_WITH_BACKWARD_COMPATIBILITY ]; then
			pldm-package-re-wrapper bic -s "$slot_id" -f "$pldm_image"
			pldm_image="${pldm_image}_re_wrapped"
		fi

		VR_TYPE="$(get_vr_type)" || { echo "parse VR_TYPE failed"; exit 1; }
		update_vr_new_bic_name "${VR_TYPE}"
	    add_init_sel

		#VR_TYPE=$(hexdump -n 1 -s 0x6D -e '1/1 "%02x"' "$pldm_image")
		vr_offsets=$(get_offset_for_vr_type "$VR_TYPE")
		ret=$?
		if [ "$ret" -ne 0 ]; then
			echo "$vr_offsets"
			exit "$ret"
		fi
		mapfile -t VR_OFFSET <<< "$vr_offsets"
		VR_HIGHBYTE_OFFSET="${VR_OFFSET[0]}"
		VR_LOWBYTE_OFFSET="${VR_OFFSET[1]}"
	elif [ "$bic_name" == "wf_vr" ]; then
		echo "Initiate WF VR update"
		if [ $ident_result -eq $PLDM_FW_IDENT_PASS ]; then
			pldm-package-re-wrapper ds -s "$slot_id" -f "$pldm_image"
			pldm_image="${pldm_image}_re_wrapped"
		elif [ $ident_result -eq $PLDM_FW_IDENT_PASS_WITH_BACKWARD_COMPATIBILITY ]; then
			pldm-package-re-wrapper bic -s "$slot_id" -f "$pldm_image"
			pldm_image="${pldm_image}_re_wrapped"
		fi

		VR_TYPE="$(get_vr_type)" || { echo "parse VR_TYPE failed"; exit 1; }
		update_vr_new_bic_name "${VR_TYPE}"
	    add_init_sel
	elif [ "$bic_name" == "sd" ]; then
		echo "Initiate SD BIC update"
		pldm-package-re-wrapper bic -s "$slot_id" -f "$pldm_image"
		pldm_image="${pldm_image}_re_wrapped"
	elif [ "$bic_name" == "wf" ]; then
		echo "Initiate WF BIC update"
		pldm-package-re-wrapper bic -s "$slot_id" -f "$pldm_image"
		pldm_image="${pldm_image}_re_wrapped"
	elif [ "$bic_name" == "sd_retimer" ]; then
		echo "Initiate SD Retimer update"
		if [ $ident_result -eq $PLDM_FW_IDENT_PASS ]; then
			pldm-package-re-wrapper ds -s "$slot_id" -f "$pldm_image"
			pldm_image="${pldm_image}_re_wrapped"
		elif [ $ident_result -eq $PLDM_FW_IDENT_PASS_WITH_BACKWARD_COMPATIBILITY ]; then
			pldm-package-re-wrapper bic -s "$slot_id" -f "$pldm_image"
			pldm_image="${pldm_image}_re_wrapped"
		fi
		check_if_no_retimer_sku "$slot_id"
		exit_code=$?
		if [ $exit_code -eq $NO_RETIMER_ON_THE_FRU ]; then
			echo "Retimer update failed, no retimer on the slot${slot_id}."
			add_result_sel "${FAILURE_MSG}"
			exit $exit_code
		elif [ $exit_code -ne 0 ]; then
			add_result_sel "${FAILURE_MSG}"
			exit $exit_code
		fi
	fi
	retry_firmware_operation || exit $?
else
	pldm_fw_identify "$bic_name" "$pldm_image"
	ident_result=$?
	if [ $ident_result -eq $PLDM_FW_IDENT_FAIL ]; then
		add_result_sel "${FAILURE_MSG}"
		exit 255
	fi
	prompt_confirmation
	if [ "$bic_name" == "sd" ]; then
		echo "Initiate SD BIC update"
	elif [ "$bic_name" == "wf" ]; then
		echo "Initiate WF BIC update"
	fi
	for i in {1..8}; do
		slot_id=$i
		pldm_image=$2
		add_init_sel

		pldm-package-re-wrapper bic -s "$slot_id" -f "$pldm_image"
		pldm_image="${pldm_image}_re_wrapped"
		retry_firmware_operation
	done
fi

if [ -z "$use_uart_update" ]; then
    [ ! -f "$pldm_image" ] && error_and_exit "PLDM"
else
    echo "Using UART mode: skip PLDM image check"
fi

#unlock
flock -u 200
