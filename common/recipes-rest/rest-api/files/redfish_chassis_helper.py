import json
import os
import time
import typing as t
from json.decoder import JSONDecodeError
from shutil import which

import aggregate_sensor as libag
import pal
import sdr
from common_utils import async_exec


SensorDetails = t.NamedTuple(
    "SensorDetails",
    [
        ("sensor_name", str),
        ("sensor_number", int),
        ("fru_name", str),
        ("reading", float),
        # Not referencing sdr.ThreshSensor as sdr is unavailable on unit test env
        # only for platforms that support libpal
        ("sensor_thresh", t.Optional["sdr.ThreshSensor"]),
        ("sensor_unit", t.Optional[str]),
        # Not referencing pal.SensorHistory as pal is unavailable on unit test env
        # only for platforms that support libpal
        ("sensor_history", t.Optional["pal.SensorHistory"]),
    ],
)

FRUID_UTIL_PATH = "/usr/local/bin/fruid-util"
WEUTIL_PATH = "/usr/bin/weutil"

""" sensor type constants as defined in sensors.py """
LIB_SENSOR_IN = 0
LIB_SENSOR_FAN = 1
LIB_SENSOR_TEMPERATURE = 2
LIB_SENSOR_POWER = 3
LIB_SENSOR_CURR = 5
LIB_SENSOR_PWM = 7

sensor_unit_dict = {
    LIB_SENSOR_FAN: "RPM",
    LIB_SENSOR_TEMPERATURE: "C",
    LIB_SENSOR_POWER: "Watts",
    LIB_SENSOR_IN: "Amps",
    LIB_SENSOR_CURR: "Volts",
    LIB_SENSOR_PWM: "%",
}
SAD_SENSOR = -99999  # default reading for values not found.


def get_pal_sensor(fru_name: str, fru_id: int, sensor_id: int) -> SensorDetails:
    sensor_unit = sdr.sdr_get_sensor_units(fru_id, sensor_id)
    try:
        reading = pal.sensor_read(fru_id, sensor_id)
        reading = reading
        start_time = int(time.time()) - 60
        sensor_history = pal.sensor_read_history(fru_id, sensor_id, start_time)
    except pal.LibPalError:
        reading = SAD_SENSOR  # default value when we can't get reading
        sensor_history = None

    try:
        sensor_thresh = sdr.sdr_get_sensor_thresh(fru_id, sensor_id)
    except sdr.LibSdrError:
        sensor_thresh = None

    try:
        sensor_name = sdr.sdr_get_sensor_name(fru_id, sensor_id)
    except sdr.LibSdrError:
        sensor_name = str(sensor_id)
    sensor_name = fru_name + "/" + fru_name + "/" + sensor_name

    if sensor_unit == "%":
        sensor_unit = "Percent"  # DMTF accepts this unit as text
    sensor_details = SensorDetails(
        sensor_name=sensor_name,
        sensor_number=sensor_id,
        fru_name=fru_name,
        reading=reading,
        sensor_thresh=sensor_thresh,
        sensor_unit=sensor_unit,
        sensor_history=sensor_history,
    )
    return sensor_details


def get_sensor_details_using_libpal(
    fru_name: str, desired_sensor_units: t.List[str]
) -> t.List[SensorDetails]:
    """Returns sensor details for platforms that support libpal.
    Those are compute and newer fboss platforms"""
    fru_name_map = pal.pal_fru_name_map()
    fru_id = fru_name_map[fru_name]
    sensor_details_list = []  # type: t.List[SensorDetails]
    if not pal.pal_is_fru_prsnt(fru_id):  # Check if the fru is present
        return sensor_details_list

    sensor_ids_list = pal.pal_get_fru_sensor_list(fru_id)
    for sensor_id in sensor_ids_list:
        sensor_details = get_pal_sensor(fru_name, fru_id, sensor_id)
        if sensor_details.sensor_unit in desired_sensor_units:
            sensor_details_list.append(sensor_details)
    return sensor_details_list


def get_sensor_details_using_libpal_helper(
    sensor_units: t.List[str], fru_name: t.Optional[str] = None
) -> t.List[SensorDetails]:
    all_sensor_details = []
    if fru_name is None:
        fru_name_list = get_single_sled_frus()
        for fru in fru_name_list:
            all_sensor_details += get_sensor_details_using_libpal(fru, sensor_units)
    else:
        all_sensor_details = get_sensor_details_using_libpal(fru_name, sensor_units)
    return all_sensor_details


def get_older_fboss_sensor_details_filtered(
    fru_name: str, desired_sensor_type: t.List[int]
) -> t.List[SensorDetails]:
    """Returns sensor details using sensors.py for older fboss
    platforms that don't support libpal i.e. yamp, wedge, wedge100"""
    desired_sensor_units = [sensor_unit_dict[typeid] for typeid in desired_sensor_type]
    all_sensors = get_older_fboss_sensor_details(fru_name)
    sensor_details_list = []
    for sensor_candidate in all_sensors:
        if sensor_candidate.sensor_unit in desired_sensor_units:
            sensor_details_list.append(sensor_candidate)
    return sensor_details_list


def get_older_fboss_sensor_details(  # noqa: C901
    fru_name: str,
) -> t.List[SensorDetails]:
    sensor_details_list = []
    import rest_sensors
    import sensors

    sensors.init()
    for chip in sensors.ChipIterator():
        for sensor in sensors.FeatureIterator(chip):
            chip_name = sensors.chip_snprintf_name(chip)
            adapter_name = sensors.get_adapter_name(chip.bus)
            try:
                adapter_name = rest_sensors.legacy_adapter_name(adapter_name)
            except AttributeError:
                # Platforms that ship their own rest_sensors.py (e.g. yamp)
                # don't define legacy_adapter_name(); keep the name unchanged.
                pass
            sensor_tag = sensor.name.decode("utf-8")
            reading_key = sensor_tag + "_input"
            ucr_key = sensor_tag + "_max"
            sensor_label = sensors.get_label(chip, sensor)
            sensor_name = chip_name + "/" + adapter_name + "/" + sensor_label
            sfs = list(sensors.SubFeatureIterator(chip, sensor))
            sf_keys = []
            sf_vals = []
            subfeatures_dict = {}
            #  retrieving keys and values for subfeatures
            for sf in sfs:
                sf_keys.append(sf.name.decode("utf-8"))
                try:
                    sf_vals.append(sensors.get_value(chip, sf.number))
                except sensors.ErrorAccessRead:
                    # in case of an error, we set default SAD_SENSOR
                    subfeatures_dict[reading_key] = SAD_SENSOR

            #  for sensors that don't have upper threshold as a feature
            if ucr_key not in sf_keys:
                subfeatures_dict[ucr_key] = 0  # default upper threshold val

            for key, val in zip(sf_keys, sf_vals):
                subfeatures_dict[key] = val
            if reading_key in subfeatures_dict:
                reading = subfeatures_dict[reading_key]
            else:
                reading = subfeatures_dict[sensor_tag]
            sensor_details = SensorDetails(
                sensor_name=sensor_name,
                sensor_number=0,  # default bc sensors.py doesn't provide sensor id
                fru_name=fru_name,
                reading=reading,
                sensor_thresh=sdr.ThreshSensor(
                    ucr_thresh=subfeatures_dict[ucr_key],
                    unc_thresh=0,
                    unr_thresh=0,
                    lcr_thresh=0,
                    lnc_thresh=0,
                    lnr_thresh=0,
                ),
                sensor_unit=sensor_unit_dict[sensor.type],
                sensor_history=None,  # bc sensors.py doesn't provide sensor history
            )
            sensor_details_list.append(sensor_details)

    sensors.cleanup()
    return sensor_details_list


def get_aggregate_sensors() -> t.List[SensorDetails]:
    """This method helps us get aggregate sensors like
    SYSTEM_AIRFLOW that we can't get using libpal and sensors.py"""
    libag.aggregate_sensor_init()
    as_size = libag.aggregate_sensor_count()
    ag_sensors_list = []  # type: t.List[SensorDetails]
    for sensor_id in range(as_size):
        sensor_details = get_aggregate_sensor(sensor_id)
        if sensor_details:
            ag_sensors_list.append(sensor_details)
    return ag_sensors_list


def get_aggregate_sensor(sensor_id: int) -> t.Optional[SensorDetails]:
    libag.aggregate_sensor_init()
    try:
        sensor_name = libag.aggregate_sensor_name(sensor_id)
    except libag.LibAggregateError:
        # If we can't get the sensor_name, we don't
        # populate this sensor. Move to next iteration
        return None
    try:
        reading = libag.aggregate_sensor_read(sensor_id)
    except libag.LibAggregateError:
        reading = SAD_SENSOR  # so we know its unhealthy sensor
    sensor_details = SensorDetails(
        sensor_name="Chassis/Chassis/" + sensor_name,
        sensor_number=sensor_id,  # set default to 0
        fru_name="aggregate",
        reading=reading,
        sensor_thresh=None,  # set to default
        sensor_unit=None,  # set to default
        sensor_history=None,  # set to default
    )
    return sensor_details


FruInfo = t.NamedTuple(
    "FruInfo",
    [
        ("fru_name", str),
        ("manufacturer", t.Optional[str]),
        ("serial_number", t.Optional[int]),
        ("model", t.Optional[str]),
    ],
)


# as a workaround for lru_cache because its not compatible with async func
__CACHE_FRU_INFO = {}  # type: t.Dict[str, FruInfo]


async def get_fru_info_helper(fru_name: t.Optional[str] = None) -> t.List[FruInfo]:
    frus_info_list = []
    if fru_name is None:  # for single sled platforms
        fru_name_list = get_single_sled_frus()
        for fru in fru_name_list:
            fru_info = await get_fru_info(fru)
            frus_info_list.append(fru_info)
    else:  # for multisled platforms
        fru_info = await get_fru_info(fru_name)
        frus_info_list.append(fru_info)

    return frus_info_list


async def get_fru_info(fru_name: str) -> FruInfo:  # noqa: C901
    if fru_name in __CACHE_FRU_INFO:
        return __CACHE_FRU_INFO[fru_name]

    fru_details_dict = {}
    product_name_key = "Product Name"
    if is_fruid_util_available():  # then its a compute platform
        # update keys as per fruid util output
        product_manufacturer_key = "Product Manufacturer"
        product_serial_key = "Product Serial"
        cmd = [FRUID_UTIL_PATH, fru_name, "--json"]
        _, fru_details, _ = await async_exec(cmd)
        try:
            fru_details_list = json.loads(fru_details)
            if fru_details_list:  # if its not empty
                fru_details_dict = fru_details_list[0]
        except JSONDecodeError:
            pass

    elif is_we_util_available():  # then its an fboss platform
        cmd = [WEUTIL_PATH]
        _, fru_details, _ = await async_exec(cmd)
        s_data = fru_details.split(os.linesep)
        for line in s_data:
            key_pair = line.split(":")
            if len(key_pair) > 1:
                fru_details_dict[key_pair[0]] = key_pair[1]
        # update keys as per weutil output
        product_manufacturer_key = "System Manufacturer"
        product_serial_key = "Product Serial Number"
    else:
        raise NotImplementedError("Can't get fru info for fru : {}".format(fru_name))

    try:
        fru_info = FruInfo(
            fru_name,
            fru_details_dict[product_manufacturer_key].strip(),
            fru_details_dict[product_serial_key].strip(),
            fru_details_dict[product_name_key].strip(),
        )
        __CACHE_FRU_INFO[fru_name] = fru_info
        return fru_info
    except KeyError:
        return FruInfo(
            fru_name=fru_name, manufacturer=None, serial_number=None, model=None
        )


def is_fruid_util_available() -> bool:
    return which(FRUID_UTIL_PATH) is not None


def is_we_util_available() -> bool:
    return which(WEUTIL_PATH) is not None


def get_single_sled_frus() -> t.List[str]:
    fru_list = []
    if is_libpal_supported():  # for compute and new fboss platforms
        fru_name_map = pal.pal_fru_name_map()
        for fru_name, fruid in fru_name_map.items():
            if (
                pal.FruCapability.FRU_CAPABILITY_HAS_DEVICE
                not in pal.pal_get_fru_capability(fruid)
                and "exp" not in fru_name
            ):
                fru_list.append(fru_name)
    # for FBOSS platforms without libpal or that don't report a server FRU.
    if len(fru_list) == 0:
        fru_list = ["BMC"]
    return fru_list


def is_libpal_supported() -> bool:
    """platforms that don't support libpal for sensor related data"""
    return bool(pal.pal_fru_name_map())


def _get_accelerator_list() -> t.List[str]:
    accelerators = []
    fru_name_map = pal.pal_fru_name_map()
    for fru_name, fruid in fru_name_map.items():
        fru_capabilities = pal.pal_get_fru_capability(fruid)
        if (
            pal.FruCapability.FRU_CAPABILITY_HAS_DEVICE in fru_capabilities
            and pal.FruCapability.FRU_CAPABILITY_SERVER not in fru_capabilities
        ):
            accelerators.append(fru_name)
    return accelerators
