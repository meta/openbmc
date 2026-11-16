import subprocess


class BoardRevision:

    BRD_TYPE_TAHANSB800BC = 0x07
    BRD_TYPE_ICECUBE = 0x08
    BRD_TYPE_ICETEA = 0x09
    BRD_TYPE_LADAKH800BCLS = 0x0D
    BRD_TYPE_LEH800BCLS = 0x0E
    BRD_TYPE_M4061CLSCL = 0x0F

    BRD_REV_EVT1 = 0x0
    BRD_REV_EVT2A = 0x1
    BRD_REV_EVT2B = 0x2
    BRD_REV_DVT1A = 0x3
    BRD_REV_DVT1B = 0x4
    BRD_REV_PPVT = 0x5
    BRD_REV_PVT = 0x6
    BRD_REV_MP = 0x7

    # Mapping of board types to platform names
    # Depend on board-utils wedge_board_type()
    platform_type = {
        BRD_TYPE_TAHANSB800BC: "TAHANSB800BC",
        BRD_TYPE_ICECUBE: "ICECUBE800BC",
        BRD_TYPE_ICETEA: "ICETEA",
        BRD_TYPE_LADAKH800BCLS: "LADAKH800BCLS",
        BRD_TYPE_LEH800BCLS: "LEH800BCLS",
        BRD_TYPE_M4061CLSCL: "M4061CLSCL",
    }

    # Mapping of board revisions to their string representations
    # Depend on board-utils wedge_board_rev()
    revision = {
        BRD_REV_EVT1: "Pre-EVT & EVT1",
        BRD_REV_EVT2A: "EVT-2A",
        BRD_REV_EVT2B: "EVT-2B",
        BRD_REV_DVT1A: "DVT-1A",
        BRD_REV_DVT1B: "DVT-1B",
        BRD_REV_PPVT: "PPVT",
        BRD_REV_PVT: "PVT",
        BRD_REV_MP: "MP",
    }


def get_platform_name_revision():
    # source board-utils to get platform name
    ret = subprocess.run(
        "/usr/local/bin/fboss-board-revision.sh -s",
        shell=True,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    if ret.returncode != 0:
        raise RuntimeError(
            f"Failed to get platform name: {ret.stderr.decode('utf-8').strip()}"
        )

    output = ret.stdout.decode("utf-8").strip()
    # separate platform name and revision from output
    # eg. ICECUBE800BC_EVT2
    platform_name = output.split("_")[0]
    platform_rev = output.split("_")[1]
    return platform_name, platform_rev


def get_platform_id():
    platform = get_platform_name_revision()

    for brd_type, name in BoardRevision.platform_type.items():
        if platform[0] == name:
            return brd_type

    return None


def get_board_revision():
    platform = get_platform_name_revision()

    for rev, text in BoardRevision.revision.items():
        if platform[1] == text:
            return rev

    return None
