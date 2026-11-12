#!/bin/bash

lockfile="/tmp/pldm-fw-update.lock"

FAIL_TO_UPDATE_WF_CXL_PLDM_SERVICE_NOT_RUNING=16
FAIL_TO_UPDATE_WF_CXL_CAN_ONLY_UPDATE_ONE_BIC_AT_A_TIME=18
FAIL_TO_UPDATE_WF_CXL_TIME_OUT_ERROR=19
FAIL_TO_UPDATE_WF_CXL_FAIL_TO_DELETE_SOFTWARE_ID=20
FAIL_TO_UPDATE_PLDM_FAIL_TO_SET_REQUESTED_ACTIVATION=21
FAIL_TO_UPDATE_WF_CXL_MISS_SOFTWARE_ID=22
FAIL_TO_UPDATE_WF_CXL_SOFTWARE_ACTIVATION_STATUS_FAIL=23
FAIL_TO_UPDATE_PLDM_IMAGE_DOES_NOT_MATCH=24
FAIL_TO_UPDATE_WF_EID_DOES_NOT_EXIST=25
INVALID_INPUT=255

exec 200>"$lockfile"
flock -n 200 || { echo "BIC update is already running"; exit 1; }

readonly WF_CXL_MSG="WF CXL"
readonly SUCCESS_MSG="Success"
readonly FAILURE_MSG="Failure"

# phosphor-dbus-interfaces define
readonly TargetDetermined="xyz.openbmc_project.Software.Update.TargetDetermined"
readonly UpdateSuccessful="xyz.openbmc_project.Software.Update.UpdateSuccessful"
readonly ActivateFailed="xyz.openbmc_project.Software.Update.ActivateFailed"

WF_EID_suffix=2
CXL1_EID_suffix=4
CXL2_EID_suffix=5

add_init_sel() {
	log-create ${TargetDetermined} --json "{\"TARGET_NAME\":\"${WF_CXL_MSG} slot${slot_id} instance_num${instance_num}\", \"IMAGE_IDENTIFIER\":\"${pldm_image}\"}"
}

add_result_sel() {
	RESULT="$1"

	if [ "${RESULT}" == "${FAILURE_MSG}" ]; then
		log-create ${ActivateFailed} --json "{\"IMAGE_IDENTIFIER\":\"${pldm_image}\", \"TARGET_NAME\":\"${WF_CXL_MSG} slot${slot_id} instance_num${instance_num}\"}"
	elif [ "${RESULT}" == "${SUCCESS_MSG}" ]; then
		log-create ${UpdateSuccessful} --json "{\"TARGET_NAME\":\"${WF_CXL_MSG} slot${slot_id} instance_num${instance_num}\", \"IMAGE_IDENTIFIER\":\"${pldm_image}\"}"
	fi
}

show_usage() {
    echo "Usage: cxl-fw-update.sh <slot_id> <instance_num> <pldm image>"
    echo "       update CXL1  : cxl-fw-update.sh <slot_id> 1 <pldm image>"
    echo "       update CXL2  : cxl-fw-update.sh <slot_id> 2 <pldm image>"
    echo ""
}

wait_for_update_complete() {
    local counter=0
    local output status progress ret
    while true;
    do
        sleep 5
        echo -ne "Waiting for updating... ${counter} sec"\\r
        output=$(busctl get-property xyz.openbmc_project.PLDM /xyz/openbmc_project/software/"$software_id" xyz.openbmc_project.Software.Activation Activation --timeout=120 2>&1)
        ret=$?
        if [ "$ret" -eq 0 ]; then
            status=$(echo "$output" | cut -d " " -f 2)
            if [ "${status}" == "\"xyz.openbmc_project.Software.Activation.Activations.Failed\"" ] || [ "${status}" == "\"xyz.openbmc_project.Software.Activation.Activations.Invalid\"" ]; then
                echo -ne \\n"PLDM software activation is ${status}."\\n
                return $FAIL_TO_UPDATE_WF_CXL_SOFTWARE_ACTIVATION_STATUS_FAIL
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
            return $FAIL_TO_UPDATE_WF_CXL_MISS_SOFTWARE_ID
        fi
        counter=$((counter+5))
        if [ "${counter}" == 400 ]; then
            echo -ne \\n"Time out. Fail"\\n
            return $FAIL_TO_UPDATE_WF_CXL_TIME_OUT_ERROR
        fi
    done
}

PLDM_FW_IDENT_PASS=0
PLDM_FW_IDENT_PASS_WITH_BACKWARD_COMPATIBILITY=1
PLDM_FW_IDENT_FAIL=255

pldm_fw_identify() {
	local pldm_image=$1
    comp_str="com.meta.Hardware.Yosemite4.WailuaFalls.CXL"

	# Check if the PLDM image is valid
	if [ ! -f "$pldm_image" ]; then
		echo "PLDM image not found: $pldm_image"
		return $PLDM_FW_IDENT_FAIL
	fi

	# Parse PLDM Header

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
    for _ in $(seq 0 $((descriptor_count - 1))); do
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
			return $PLDM_FW_IDENT_FAIL
		fi
	done

	((offset+=4))
	component_identifier=$(hexdump -v -n 2 -s "$offset" -e '1/2 "%u"' "$pldm_image")
	((offset+=2))

    if [ "$component_identifier" -lt 5 ] || [ "$component_identifier" -gt 6 ]; then
        return $PLDM_FW_IDENT_FAIL
    fi
    if [ "$pci_id" != "0002" ]; then
        return $PLDM_FW_IDENT_FAIL
    fi

    return $PLDM_FW_IDENT_PASS_WITH_BACKWARD_COMPATIBILITY
}

update_cxl() {
    local pldm_image=$1

    cp "$pldm_image" /tmp/pldm_images

    sleep 1

    software_id=$(busctl tree xyz.openbmc_project.PLDM | grep "/xyz/openbmc_project/software/[0-9]\+" | awk -F'/' '{print $5}' | head -n1)
    echo "software_id = $software_id"
    sleep 1

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
        ret=$?
        if [ "$ret" -ne 0 ]; then
            return $ret
        fi
    else
        echo "Fail: Miss software id."
        return $FAIL_TO_UPDATE_WF_CXL_MISS_SOFTWARE_ID
    fi
    return 0
}

delete_software_id() {
    local output ret
    if [ -z "$software_id" ]; then
        return 0
    fi

    output=$(busctl call xyz.openbmc_project.PLDM /xyz/openbmc_project/software/"$software_id" xyz.openbmc_project.Object.Delete Delete --timeout=120 2>&1)
    ret=$?
    if [ "$ret" -ne 0 ]; then
        echo "Failed to delete software id $software_id: $output"
        return $FAIL_TO_UPDATE_WF_CXL_FAIL_TO_DELETE_SOFTWARE_ID
    fi
    software_id=""
    return 0
}

error_and_exit() {
    echo "Please provide valid PLDM image"
    show_usage
    exit "$INVALID_INPUT"
}

MAX_RETRIES=3

retry_update_cxl() {
    local retries=0
    local ret=0

    update_cxl "$pldm_image"
    ret=$?
    if [ $ret -eq 0 ]; then
        return 0
    fi

    while [ $retries -lt $MAX_RETRIES ]; do
        retries=$((retries + 1))
        echo "CXL firmware update failed. Retrying... ($retries/$MAX_RETRIES)"
        if ! delete_software_id; then
            echo "Warning: Failed to delete previous software id before retry"
        fi
        # each retry before restart pldmd
        systemctl stop pldmd
        sleep 10
        systemctl start pldmd
        sleep 60
        software_id=""
        update_cxl "$pldm_image"
        ret=$?
        if [ $ret -eq 0 ]; then
            return 0
        fi
    done

    if ! delete_software_id; then
        echo "Warning: Failed to delete software id after retries exhausted"
    fi
    echo "Maximum retries ($MAX_RETRIES) reached. Exiting with error code: $ret."
    return $ret
}

# Check for minimum required arguments
[ $# -ne 3 ] && error_and_exit

slot_id=$1
instance_num=$2
pldm_image=$3

[ ! -f "$pldm_image" ] && error_and_exit

add_init_sel

pldm_fw_identify "$pldm_image"
ident_result=$?
if [ $ident_result -eq $PLDM_FW_IDENT_FAIL ]; then
    echo "This image is not compatible with CXL"
    add_result_sel "${FAILURE_MSG}"
    exit "$INVALID_INPUT"
fi

# Check if instance_num is between 1 and 2
if [[ "$instance_num" =~ ^[1-2]$ ]]; then
    pldm-package-re-wrapper cxl -s "$slot_id" -i "$instance_num" -f "$pldm_image"
    pldm_image="${pldm_image}_re_wrapped"
else
    echo "Instance number must between 1 and 2."
    add_result_sel "${FAILURE_MSG}"
    exit "$INVALID_INPUT"
fi

if ! systemctl is-active --quiet pldmd; then
    echo "STOP. PLDM service is not running. Please check pldmd status."
    add_result_sel "${FAILURE_MSG}"
    exit "$FAIL_TO_UPDATE_WF_CXL_PLDM_SERVICE_NOT_RUNING"
fi

#Show all mctp eids, and check cxl eid should exist
#The cxl eid would end with 4 or 5.
mctp_output=$(busctl tree au.com.codeconstruct.MCTP1 --timeout=120 2>&1 | grep "/au/com/codeconstruct/mctp1/networks/1/endpoints")
echo "Check MCTP EID"
echo "$mctp_output"
if ! echo "$mctp_output" | grep -qE "/au/com/codeconstruct/mctp1/networks/1/endpoints/$((slot_id * 10 + (instance_num == 1 ? CXL1_EID_suffix : CXL2_EID_suffix)))"; then
  echo "WARNING! The CXL EID $((slot_id * 10 + (instance_num == 1 ? CXL1_EID_suffix : CXL2_EID_suffix))) does not exist."
  if ! echo "$mctp_output" | grep -qE "/au/com/codeconstruct/mctp1/networks/1/endpoints/$((slot_id * 10 + (WF_EID_suffix)))"; then
    echo "Not allowed. The WF EID $((slot_id * 10 + (WF_EID_suffix))) does not exist."
    add_result_sel "${FAILURE_MSG}"
    exit "$FAIL_TO_UPDATE_WF_EID_DOES_NOT_EXIST"
  fi
fi


echo "Check if other BICs are updating"
pldm_output=$(busctl tree xyz.openbmc_project.PLDM --timeout=120)
if echo "$pldm_output" | grep -qE "/xyz/openbmc_project/software/[0-9]+"; then
echo "$pldm_output" | grep -E "/xyz/openbmc_project/software/[0-9]+"
previous_software_id=$(echo "$pldm_output" | grep /xyz/openbmc_project/software/ | cut -d "/" -f 5)
busctl get-property xyz.openbmc_project.PLDM /xyz/openbmc_project/software/"$previous_software_id" xyz.openbmc_project.Software.ActivationProgress Progress --timeout=120 > /dev/null
ret=$?
    if [ "$ret" -eq 0 ]; then
        echo "It can only be updated one BIC at a time. Please wait until the software update is completed."
        add_result_sel "${FAILURE_MSG}"
        exit "$FAIL_TO_UPDATE_WF_CXL_CAN_ONLY_UPDATE_ONE_BIC_AT_A_TIME"
    fi
fi


retry_update_cxl
ret=$?
if [ "$ret" -ne 0 ]; then
    echo "Firmware update failed after retries. Exit code: $ret"
    add_result_sel "${FAILURE_MSG}"
    flock -u 200
    exit "$ret"
fi

if ! delete_software_id; then
    echo "Warning: Failed to delete software id after successful update"
fi

add_result_sel "${SUCCESS_MSG}"
echo "Done. If there was no error appeared, please conduct DC cycle to load the new firmware."

#unlock
flock -u 200
