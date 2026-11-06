import asyncio
import sys
import types
import typing as t
import unittest
import unittest.mock

import aiohttp.web
import pal
import redfish_chassis_helper
import sdr
import test_mock_modules  # noqa: F401
from aiohttp.test_utils import AioHTTPTestCase, unittest_run_loop
from common_middlewares import jsonerrorhandler


class FakeAggregateError(Exception):
    pass


# mocking a tuple instead of sdr.ThreshSensor bc sdr lib isn't available here
sensor_thresh = t.NamedTuple(
    "sensor_thresh",
    [
        ("unc_thresh", float),
        ("ucr_thresh", float),
        ("unr_thresh", float),
        ("lnc_thresh", float),
        ("lcr_thresh", float),
        ("lnr_thresh", float),
    ],
)

fake_sensors_py_sensor_details = redfish_chassis_helper.SensorDetails(
    sensor_name="dummy_snr",
    sensor_number=0,
    fru_name="BMC",
    reading=333,
    sensor_thresh=None,
    sensor_unit="Volts",
    sensor_history=None,
)


class TestRedfishSensors(AioHTTPTestCase):
    async def setUpAsync(self):
        asyncio.set_event_loop(asyncio.new_event_loop())

        # mocking a tuple instead of pal.SensorHistory bc pal lib isn't available here
        SensorHistory = t.NamedTuple(
            "SensorHistory",
            [
                ("min_intv_consumed", float),
                ("max_intv_consumed", float),
                ("avg_intv_consumed", float),
            ],
        )

        self.patches = [
            unittest.mock.patch(
                "rest_pal_legacy.pal_get_num_slots",
                create=True,
                return_value=4,
            ),
            unittest.mock.patch(
                "pal.pal_get_platform_name",
                create=True,
                return_value="fby2",
            ),
            unittest.mock.patch(
                "pal.pal_fru_name_map",
                create=True,
                return_value={"slot1": 1, "slot2": 2, "slot3": 3, "slot4": 4, "spb": 5},
            ),
            unittest.mock.patch(
                "pal.pal_is_fru_prsnt",
                create=True,
                return_value=True,
            ),
            unittest.mock.patch(
                "pal.pal_get_fru_sensor_list",
                create=True,
                side_effect=[[224, 123], [224, 123]],
            ),
            unittest.mock.patch(
                "sdr.sdr_get_sensor_units",
                create=True,
                side_effect=["Amps", "C", "RPM"],
            ),
            unittest.mock.patch(
                "pal.sensor_read",
                create=True,
                side_effect=[24.9444444444, 7177],
            ),
            unittest.mock.patch(
                "sdr.sdr_get_sensor_thresh",
                create=True,
                side_effect=[
                    sensor_thresh(
                        0,
                        5,
                        0,
                        0,
                        0,
                        0,
                    ),
                ],
            ),
            unittest.mock.patch(
                "pal.sensor_read_history",
                new_callable=unittest.mock.MagicMock,
                create=True,
                return_value=SensorHistory(5.02, 5.03, 5.03),
            ),
            unittest.mock.patch(
                "sdr.sdr_get_sensor_name",
                create=True,
                side_effect=["SP_P5V", "SP_INLET_TEMP", "SP_FAN0_TACH"],
            ),
            unittest.mock.patch(
                "pal.LibPalError",
                create=True,
                new=type("LibPalError", (Exception,), {}),
            ),
            unittest.mock.patch(
                "redfish_chassis_helper.get_fru_info",
                new_callable=unittest.mock.MagicMock,  # python < 3.8 compat
                return_value=asyncio.Future(),
            ),
            unittest.mock.patch(
                "aggregate_sensor.aggregate_sensor_count",
                create=True,
                return_value=1,
            ),
            unittest.mock.patch(
                "aggregate_sensor.aggregate_sensor_init",
                create=True,
                return_value=None,
            ),
            unittest.mock.patch(
                "aggregate_sensor.aggregate_sensor_read",
                create=True,
                return_value=123,
            ),
            unittest.mock.patch(
                "aggregate_sensor.aggregate_sensor_name",
                create=True,
                return_value="aggregate_sensor",
            ),
            unittest.mock.patch(
                "aggregate_sensor.LibAggregateError",
                create=True,
                new=FakeAggregateError,
            ),
            unittest.mock.patch(
                "redfish_sensors._get_fru_names",
                side_effect=[["spb"], ["slot1"], ["slot2"], ["slot3"], ["slot4"]],
            ),
            unittest.mock.patch(
                "redfish_sensors.fru_name_map",
                new={
                    "spb": 1,
                    "nic": 2,
                    "slot1": 3,
                    "slot2": 4,
                    "slot3": 5,
                    "slot4": 6,
                },
            ),
            unittest.mock.patch(
                "redfish_chassis_helper.get_older_fboss_sensor_details",
                return_value=[fake_sensors_py_sensor_details],
            ),
        ]
        for p in self.patches:
            p.start()
            self.addCleanup(p.stop)
        await super().setUpAsync()

    def get_fru_name(self, server_name: str) -> str:
        if server_name == "1":
            return "spb"  # default to assert single sled frus
        else:
            #  if not a single sled fru then return the correct fru_name for assertion
            return server_name.replace("server", "slot")

    @unittest_run_loop
    async def test_get_chassis_sensors_works_with_pal(self):
        "Testing sensor response for both single sled frus and multisled frus"
        for server_name in ["1", "server1", "server2", "server3", "server4"]:
            with self.subTest(server_name=server_name):
                fru_name = self.get_fru_name(server_name)
                pal.pal_get_fru_sensor_list.side_effect = [[224]]
                sdr.sdr_get_sensor_units.side_effect = ["Amps"]
                sdr.sdr_get_sensor_name.side_effect = ["SP_P5V"]
                pal.sensor_read.side_effect = [
                    0
                ]  # placeholder to avoid running out of side_effect vals
                sdr.sdr_get_sensor_thresh.side_effect = [
                    sensor_thresh(
                        0,
                        5.5,
                        0,
                        0,
                        0,
                        0,
                    ),
                ]
                expected_resp = {
                    "@odata.id": "/redfish/v1/Chassis/{parent_name}/Sensors".format(
                        parent_name=server_name
                    ),
                    "@odata.type": "#SensorCollection.SensorCollection",
                    "Members": [
                        {
                            "@odata.id": "/redfish/v1/Chassis/{parent_name}/Sensors/{fru_name}_224".format(  # noqa: B950
                                parent_name=server_name, fru_name=fru_name
                            )
                        }
                    ],
                    "Members@odata.count": 1,
                    "Name": "Chassis sensors",
                }
                if server_name == "1":
                    expected_resp["Members"].append(
                        {"@odata.id": "/redfish/v1/Chassis/1/Sensors/aggregate_0"}
                    )
                    expected_resp["Members@odata.count"] = 2
                req = await self.client.request(
                    "GET", "/redfish/v1/Chassis/{}/Sensors".format(server_name)
                )
                resp = await req.json()
                self.maxDiff = None
                self.assertEqual(resp, expected_resp)
                self.assertEqual(req.status, 200)

    @unittest_run_loop
    async def test_get_chassis_sensors_expand_works_with_pal(self):
        "Testing sensor response for both single sled frus and multisled frus"
        for server_name in ["1", "server1", "server2", "server3", "server4"]:
            with self.subTest(server_name=server_name):
                fru_name = self.get_fru_name(server_name)
                pal.pal_get_fru_sensor_list.side_effect = [[224]]
                sdr.sdr_get_sensor_units.side_effect = ["Amps"]
                sdr.sdr_get_sensor_name.side_effect = ["SP_P5V"]
                pal.sensor_read.side_effect = [
                    0
                ]  # placeholder to avoid running out of side_effect vals
                sdr.sdr_get_sensor_thresh.side_effect = [
                    sensor_thresh(
                        0,
                        5.5,
                        0,
                        0,
                        0,
                        0,
                    ),
                ]
                expected_resp = {
                    "@odata.id": "/redfish/v1/Chassis/{parent_name}/Sensors".format(
                        parent_name=server_name
                    ),
                    "@odata.type": "#SensorCollection.SensorCollection",
                    "Members": [
                        {
                            "@odata.id": "/redfish/v1/Chassis/{parent_name}/Sensors/{fru_name}_224".format(  # noqa: B950
                                parent_name=server_name, fru_name=fru_name
                            ),
                            "@odata.type": "#Sensor.v1_2_0.Sensor",
                            "Id": "224",
                            "Name": "{fru_name}/{fru_name}/SP_P5V".format(
                                fru_name=fru_name
                            ),
                            "Oem": {},
                            "PhysicalContext": "Chassis",
                            "Reading": 0,
                            "ReadingRangeMax": 99999,
                            "ReadingRangeMin": -99999,
                            "ReadingUnits": "Amps",
                            "Status": {"Health": "OK", "State": "Enabled"},
                            "Thresholds": {
                                "LowerCaution": {"Reading": 0},
                                "LowerCritical": {"Reading": 0},
                                "LowerFatal": {"Reading": 0},
                                "UpperCaution": {"Reading": 0},
                                "UpperCritical": {"Reading": 5},
                                "UpperFatal": {"Reading": 0},
                            },
                        }
                    ],
                    "Members@odata.count": 1,
                    "Name": "Chassis sensors",
                }
                if server_name == "1":
                    expected_resp["Members"].append(
                        {
                            "@odata.id": "/redfish/v1/Chassis/1/Sensors/aggregate_0",
                            "@odata.type": "#Sensor.v1_2_0.Sensor",
                            "Id": "0",
                            "Name": "Chassis/Chassis/aggregate_sensor",
                            "Oem": {},
                            "PhysicalContext": "Chassis",
                            "Reading": 123,
                            "ReadingRangeMax": 99999,
                            "ReadingRangeMin": -99999,
                            "ReadingUnits": None,
                            "Status": {"Health": "OK", "State": "Enabled"},
                            "Thresholds": {},
                        }
                    )
                    expected_resp["Members@odata.count"] = 2
                req = await self.client.request(
                    "GET",
                    "/redfish/v1/Chassis/{}/Sensors?$expand=1".format(server_name),
                )
                resp = await req.json()
                self.maxDiff = None
                self.assertEqual(resp, expected_resp)
                self.assertEqual(req.status, 200)

    @unittest_run_loop
    async def test_get_chassis_sensor_sensorid_works_with_pal(self):
        "Testing individual sensor response for both single sled frus and multisled frus"  # noqa: B950
        for server_name in ["1", "server1", "server2", "server3", "server4"]:
            with self.subTest(server_name=server_name):
                fru_name = self.get_fru_name(server_name)
                pal.pal_get_fru_sensor_list.side_effect = [[224]]
                sdr.sdr_get_sensor_units.side_effect = ["Amps"]
                sdr.sdr_get_sensor_name.side_effect = ["SP_P5V"]
                pal.sensor_read.side_effect = [
                    0
                ]  # placeholder to avoid running out of side_effect vals
                sdr.sdr_get_sensor_thresh.side_effect = [
                    sensor_thresh(
                        0,
                        5.5,
                        0,
                        0,
                        0,
                        0,
                    ),
                ]
                expected_resp = {
                    "@odata.id": "/redfish/v1/Chassis/{parent_name}/Sensors/{fru_name}_224".format(  # noqa: B950
                        parent_name=server_name, fru_name=fru_name
                    ),
                    "@odata.type": "#Sensor.v1_2_0.Sensor",
                    "Id": "224",
                    "Name": "{fru_name}/{fru_name}/SP_P5V".format(fru_name=fru_name),
                    "Oem": {},
                    "PhysicalContext": "Chassis",
                    "Reading": 0,
                    "ReadingRangeMax": 99999,
                    "ReadingRangeMin": -99999,
                    "ReadingUnits": "Amps",
                    "Status": {"Health": "OK", "State": "Enabled"},
                    "Thresholds": {
                        "LowerCaution": {"Reading": 0},
                        "LowerCritical": {"Reading": 0},
                        "LowerFatal": {"Reading": 0},
                        "UpperCaution": {"Reading": 0},
                        "UpperCritical": {"Reading": 5},
                        "UpperFatal": {"Reading": 0},
                    },
                }
                req = await self.client.request(
                    "GET",
                    "/redfish/v1/Chassis/{parent_name}/Sensors/{fru_name}_224".format(
                        parent_name=server_name, fru_name=fru_name
                    ),
                )
                resp = await req.json()
                self.maxDiff = None
                self.assertEqual(resp, expected_resp)
                self.assertEqual(req.status, 200)

    @unittest_run_loop
    async def test_get_chassis_sensors_works_with_sensors_py(self):
        "Testing sensor response for platforms with sensors.py"
        with unittest.mock.patch(
            "redfish_chassis_helper.is_libpal_supported", return_value=False
        ):
            expected_resp = {
                "@odata.id": "/redfish/v1/Chassis/1/Sensors",
                "@odata.type": "#SensorCollection.SensorCollection",
                "Members": [
                    {"@odata.id": "/redfish/v1/Chassis/1/Sensors/dummy_snr"},
                    {
                        "@odata.id": "/redfish/v1/Chassis/1/Sensors/Chassis_Chassis_aggregate_sensor"
                    },
                ],
                "Members@odata.count": 2,
                "Name": "Chassis sensors",
            }
            req = await self.client.request("GET", "/redfish/v1/Chassis/1/Sensors")
            resp = await req.json()
            self.maxDiff = None
            self.assertEqual(resp, expected_resp)
            self.assertEqual(req.status, 200)

    @unittest_run_loop
    async def test_get_chassis_sensors_expand_works_with_sensors_py(self):
        "Testing sensor response for platforms with sensors.py"
        with unittest.mock.patch(
            "redfish_chassis_helper.is_libpal_supported", return_value=False
        ):
            expected_resp = {
                "@odata.id": "/redfish/v1/Chassis/1/Sensors",
                "@odata.type": "#SensorCollection.SensorCollection",
                "Members": [
                    {
                        "@odata.id": "/redfish/v1/Chassis/1/Sensors/dummy_snr",
                        "@odata.type": "#Sensor.v1_2_0.Sensor",
                        "Id": "0",
                        "Name": "dummy_snr",
                        "Oem": {},
                        "PhysicalContext": "Chassis",
                        "Reading": 333,
                        "ReadingRangeMax": 99999,
                        "ReadingRangeMin": -99999,
                        "ReadingUnits": "Volts",
                        "Status": {"Health": "OK", "State": "Enabled"},
                        "Thresholds": {},
                    },
                    {
                        "@odata.id": "/redfish/v1/Chassis/1/Sensors/Chassis_Chassis_aggregate_sensor",
                        "@odata.type": "#Sensor.v1_2_0.Sensor",
                        "Id": "0",
                        "Name": "Chassis/Chassis/aggregate_sensor",
                        "Oem": {},
                        "PhysicalContext": "Chassis",
                        "Reading": 123,
                        "ReadingRangeMax": 99999,
                        "ReadingRangeMin": -99999,
                        "ReadingUnits": None,
                        "Status": {"Health": "OK", "State": "Enabled"},
                        "Thresholds": {},
                    },
                ],
                "Members@odata.count": 2,
                "Name": "Chassis sensors",
            }
            req = await self.client.request(
                "GET", "/redfish/v1/Chassis/1/Sensors?$expand=1"
            )
            resp = await req.json()
            self.maxDiff = None
            self.assertEqual(resp, expected_resp)
            self.assertEqual(req.status, 200)

    @unittest_run_loop
    async def test_get_chassis_sensors_sensorid_works_with_sensors_py(self):
        "Testing sensor response for platforms with sensors.py"
        with unittest.mock.patch(
            "redfish_chassis_helper.is_libpal_supported", return_value=False
        ):
            expected_resp = {
                "@odata.id": "/redfish/v1/Chassis/1/Sensors/dummy_snr",
                "@odata.type": "#Sensor.v1_2_0.Sensor",
                "Id": "0",
                "Name": "dummy_snr",
                "Oem": {},
                "PhysicalContext": "Chassis",
                "Reading": 333,
                "ReadingRangeMax": 99999,
                "ReadingRangeMin": -99999,
                "ReadingUnits": "Volts",
                "Status": {"Health": "OK", "State": "Enabled"},
                "Thresholds": {},
            }
            req = await self.client.request(
                "GET", "/redfish/v1/Chassis/1/Sensors/dummy_snr"
            )
            resp = await req.json()
            self.maxDiff = None
            self.assertEqual(resp, expected_resp)
            self.assertEqual(req.status, 200)

    async def get_application(self):
        import redfish_sensors

        webapp = aiohttp.web.Application(middlewares=[jsonerrorhandler])
        webapp.router.add_get(
            "/redfish/v1/Chassis/{fru_name}/Sensors",
            redfish_sensors.get_redfish_sensors_handler,
        )
        webapp.router.add_get(
            "/redfish/v1/Chassis/{fru_name}/Sensors/{sensor_id}",
            redfish_sensors.get_redfish_sensor_handler,
        )
        return webapp


class TestOlderFbossSensorDetails(unittest.TestCase):
    def _sensor_names(self, rest_sensors_module) -> t.List[str]:
        chip = unittest.mock.Mock(bus=1)
        feature = unittest.mock.Mock(type=redfish_chassis_helper.LIB_SENSOR_TEMPERATURE)
        feature.name = b"temp1"
        subfeature = unittest.mock.Mock(number=0)
        subfeature.name = b"temp1_input"
        fake_sensors = types.SimpleNamespace(
            init=unittest.mock.Mock(),
            cleanup=unittest.mock.Mock(),
            ChipIterator=lambda: [chip],
            FeatureIterator=lambda chip: [feature],
            SubFeatureIterator=lambda chip, feature: [subfeature],
            chip_snprintf_name=lambda chip: "tmp75-i2c-1-48",
            get_adapter_name=lambda bus: "1e78a100.i2c",
            get_label=lambda chip, feature: "TEMP",
            get_value=lambda chip, number: 42.0,
            ErrorAccessRead=Exception,
        )
        with unittest.mock.patch.dict(
            sys.modules, {"rest_sensors": rest_sensors_module, "sensors": fake_sensors}
        ), unittest.mock.patch.object(sdr, "ThreshSensor", sensor_thresh, create=True):
            details = redfish_chassis_helper.get_older_fboss_sensor_details("BMC")
        return [d.sensor_name for d in details]

    def test_platform_rest_sensors_without_legacy_adapter_name(self):
        # yamp ships its own rest_sensors.py without legacy_adapter_name()
        self.assertEqual(
            self._sensor_names(types.SimpleNamespace()),
            ["tmp75-i2c-1-48/1e78a100.i2c/TEMP"],
        )

    def test_rest_sensors_legacy_adapter_name_is_applied(self):
        rest_sensors = types.SimpleNamespace(
            legacy_adapter_name=lambda name: "1e78a100.i2c-bus"
        )
        self.assertEqual(
            self._sensor_names(rest_sensors),
            ["tmp75-i2c-1-48/1e78a100.i2c-bus/TEMP"],
        )
