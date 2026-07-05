#!/usr/local/bin/python3
import json
import os
import platform
import re
import shutil
import ssl
import subprocess
import sys
import tempfile
import zipfile
import wget


def require_ios_host_if_needed(target):
    if target != "IOS":
        return

    if platform.system() != "Darwin":
        raise SystemExit("Boost iOS build must run on macOS because it requires Xcode and Apple iOS SDKs.")

    if shutil.which("xcodebuild") is None:
        raise SystemExit("Boost iOS build requires xcodebuild. Install Xcode and select it with xcode-select before running ThirdParty/Build_IOS.sh.")


def is_valid_ios_deployment_target(value):
    if not value or value.startswith(".") or value.endswith("."):
        return False

    has_dot = False
    previous_dot = False
    for character in value:
        if character == ".":
            if previous_dot:
                return False

            has_dot = True
            previous_dot = True
            continue

        if character < "0" or character > "9":
            return False

        previous_dot = False

    return has_dot


def get_ios_min_version():
    configured_target = os.environ.get("VE_IOS_DEPLOYMENT_TARGET", "")
    if configured_target:
        return configured_target

    detected_target = detect_latest_ios_sdk_version()
    return detected_target or "16.4"


def validate_ios_deployment_target_if_needed(target, value):
    if target != "IOS":
        return

    if not is_valid_ios_deployment_target(value):
        raise SystemExit("Invalid VE_IOS_DEPLOYMENT_TARGET for Boost iOS build: %s. Use a numeric version such as 16.4." % value)


def parse_version_parts(value):
    return tuple(int(part) for part in value.split(".") if part.isdigit())


def run_capture(*args):
    result = subprocess.run(args, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        return ""

    return result.stdout.strip()


def detect_latest_ios_sdk_version():
    if platform.system() != "Darwin":
        return ""

    version = run_capture("xcrun", "--sdk", "iphoneos", "--show-sdk-version")
    if is_valid_ios_deployment_target(version):
        return version

    sdk_text = run_capture("xcodebuild", "-showsdks")
    versions = [value for value in re.findall(r"-sdk\s+iphoneos([0-9]+(?:\.[0-9]+)+)", sdk_text) if is_valid_ios_deployment_target(value)]
    if not versions:
        return ""

    return max(versions, key=parse_version_parts)


script_file_path = sys.argv[0]
print("script_file_path: %s" % script_file_path)

boost_version = sys.argv[1]
print("boost_version: %s" % boost_version)

target_platform = sys.argv[2]
print("target_platform: %s" % target_platform)

ios_min_version = get_ios_min_version()
print("ios_min_version: %s" % ios_min_version)
validate_ios_deployment_target_if_needed(target_platform, ios_min_version)
require_ios_host_if_needed(target_platform)

script_file_dir = os.path.abspath(os.path.dirname(script_file_path))
print("script_file_dir: %s" % script_file_dir)

current_dir = os.path.abspath(os.curdir)
print("current_dir: %s" % current_dir)

required_boost_components = [
    "--with-json",
    "--with-log",
    "--with-system",
]

ios_architectures ={
    "device":
        {
            "arch": "arm64",
            "sdk": "iphoneos",
            "install_path": "device"
        },
    "simulator":
        {
            "arch": "arm64",
            "sdk": "iphonesimulator",
            "install_path": "simulator"
        },
}

if target_platform == "Windows64":
    boost_zip_file = "boost_%s.zip" % boost_version.replace(".", "_")
elif target_platform == "Mac":
    boost_zip_file = "boost_%s.tar.bz2" % boost_version.replace(".", "_")
elif target_platform == "IOS":
    boost_zip_file = "boost_%s.tar.bz2" % boost_version.replace(".", "_")
else:
    raise Exception("Unknown target platform: %s" % target_platform)

boost_url = "https://archives.boost.io/release/%s/source/%s" % (boost_version, boost_zip_file)
print("boost_url: %s" % boost_url)

boost_zip_file_path = os.path.join(current_dir, boost_zip_file)
print("boost_zip_file_path: %s" % boost_zip_file_path)

boost_root_dir = os.path.join(current_dir, ("boost_%s" % boost_version).replace(".", "_"))
print("boost_root_dir: %s" % boost_root_dir)

boost_install_path = os.path.join(current_dir, "Build")
boost_install_path = os.path.join(boost_install_path, target_platform)
print("boost_install_path: %s" % boost_install_path)

def print_step(step_name):
    sep = "*" * 80
    space_count = int((80 - len(step_name) - 10) / 2)
    step = "*" * 5 + " " * space_count + step_name + " " * space_count + "*" * 5
    print(sep)
    print(step)
    print(sep)


def check_root_dir():
    print("current path: %s" % current_dir)
    if script_file_dir != current_dir:
        print("current_path should be %s" % script_file_dir)
        exit(1)


def download_from_url():
    if os.path.exists(boost_zip_file):
        print("%s exist." % boost_zip_file)
        return

    print_step("download")
    print("download_from_url: %s" % boost_url)
    ssl._create_default_https_context = ssl._create_unverified_context
    wget.download(boost_url)
    print("")


def unzip_files_windows64():
    print_step("unzip files")
    if not os.path.exists(boost_zip_file):
        raise Exception("boost_zip_file not exist: %s" % boost_zip_file)

    with zipfile.ZipFile(boost_zip_file_path) as boost:
        boost.extractall(current_dir)

    print("extracted %s to %s" % (boost_zip_file, current_dir))

def unzip_files_mac():
    print_step("unzip files")
    if not os.path.exists(boost_zip_file):
        raise Exception("boost_zip_file not exist: %s" % boost_zip_file)
    subprocess.run(["tar", "xjf", boost_zip_file], check=True)


def build_boost_mac():
    print_step("build_boost_mac")
    os.chdir(boost_root_dir)
    if not os.path.exists("b2"):
        subprocess.run(["./bootstrap.sh"], check=True)

    with open(os.path.join(boost_root_dir, "project-config.jam"), "w") as config:
        config.write("# Boost.Build Configuration\n")
        config.write("# Generated by VEngine ThirdParty setup.\n\n")
        config.write("using clang : darwin : clang++ ;\n")
        config.write("option.set keep-going : false ;\n")

    cmd = [
        "./b2",
        "install",
        "-d0",
        "link=static",
        "threading=multi",
        "target-os=darwin",
        "architecture=arm",
        "address-model=64",
        "variant=debug,release",
        f"--prefix={boost_install_path}",
        *required_boost_components,
    ]
    print("cmd: %s" % cmd)
    subprocess.run(cmd, check=True)


def build_boost_windows64():
    print_step("build_boost_windows64")
    os.chdir(boost_root_dir)
    boost_bootstrap_path = os.path.join(boost_root_dir, "bootstrap.bat")
    boost_b2_path = os.path.join(boost_root_dir, "b2.exe")

    bootstrap_cmd = [boost_bootstrap_path, "vc143"]
    print("bootstrap_cmd: %s" % bootstrap_cmd)
    subprocess.run(bootstrap_cmd, check=True)

    if not os.path.exists(boost_b2_path):
        print("%s should exist!" % boost_b2_path)
        exit(1)

    with open(os.path.join(boost_root_dir, "project-config.jam"), "w") as config:
        config.write("# Boost.Build Configuration\n")
        config.write("# Generated by VEngine ThirdParty setup.\n\n")
        config.write("import option ;\n\n")
        config.write("using msvc : 14.3 : cl ;\n\n")
        config.write("option.set keep-going : false ;\n")

    # https://www.boost.org/doc/libs/1_85_0/more/getting_started/windows.html
    cmd = [
        "./b2",
        "install",
        "-d0",
        "link=static",
        "threading=multi",
        "target-os=windows",
        "address-model=64",
        "architecture=x86",
        "variant=debug,release",
        f"--prefix={boost_install_path}",
        *required_boost_components,
    ]
    print("cmd: %s" % cmd)
    subprocess.run(cmd, check=True)
    #os.system("%s install link=static --prefix=%s " % (boost_b2_path, boost_install_path))


def build_ios_b2():
    print_step("build_ios_b2")
    os.chdir(boost_root_dir)
    if not os.path.exists("b2"):
        subprocess.run(["./bootstrap.sh"], check=True)
    else:
        print("b2 exist!")


def get_ios_sdk_path(config):
    try:
        return subprocess.check_output([
            "xcodebuild",
            "-version",
            "-sdk",
            config["sdk"],
            "Path"
        ], text=True).strip()
    except subprocess.CalledProcessError:
        raise Exception("find sdk failed")


def get_mach_o_platform(object_file_path):
    output = subprocess.check_output([
        "vtool",
        "-show-build",
        object_file_path,
    ], text=True, stderr=subprocess.STDOUT)

    versionMinPlatformMap = {
        "LC_VERSION_MIN_IPHONEOS": "IOS",
        "LC_VERSION_MIN_IPHONEOS_SIMULATOR": "IOSSIMULATOR",
        "LC_VERSION_MIN_MACOSX": "MACOS",
        "LC_VERSION_MIN_WATCHOS": "WATCHOS",
        "LC_VERSION_MIN_TVOS": "TVOS",
    }

    for line in output.splitlines():
        strippedLine = line.strip()
        if strippedLine.startswith("cmd "):
            commandName = strippedLine[len("cmd "):]
            if commandName in versionMinPlatformMap:
                return versionMinPlatformMap[commandName]
        if strippedLine.startswith("platform "):
            return strippedLine[len("platform "):].strip()

    raise Exception(f"Unable to determine Mach-O platform for: {object_file_path}")


def validate_boost_ios_archive_platform(archive_path, expected_platform):
    with tempfile.TemporaryDirectory(prefix="vengine-boost-validate-") as temp_dir:
        members = subprocess.check_output([
            "ar",
            "-t",
            archive_path,
        ], text=True).splitlines()

        object_members = [member for member in members if member.endswith(".o")]
        if not object_members:
            raise Exception(f"No object files found in archive: {archive_path}")

        subprocess.run([
            "ar",
            "-x",
            archive_path,
            *object_members,
        ], cwd=temp_dir, check=True)

        for object_member in object_members:
            object_file_path = os.path.join(temp_dir, object_member)
            actual_platform = get_mach_o_platform(object_file_path)
            if actual_platform != expected_platform:
                raise Exception(
                    f"Boost archive has wrong Mach-O platform: {archive_path} "
                    f"member {object_member} expected {expected_platform}, got {actual_platform}"
                )


def build_boost_ios_lib(config):
    print_step("build_boost_ios_lib")
    sdk_path = get_ios_sdk_path(config)

    if config["sdk"] == "iphoneos":
        target = "arm64-apple-ios"
        version = f"-miphoneos-version-min={ios_min_version}"
        expected_platform = "IOS"
    else:
        target = "arm64-apple-ios-simulator"
        version = f"-miphonesimulator-version-min={ios_min_version}"
        expected_platform = "IOSSIMULATOR"

    common_flags = [
        f"-target {target}",
        f"-arch {config['arch']}",
        f"-isysroot {sdk_path}",
        f"-fvisibility=hidden", # to avoid boost log weak symbols warning
        f"-fvisibility-inlines-hidden", # to avoid boost log weak symbols warning
        version,
    ]
    cflags = [
        *common_flags,
    ]
    cxxflags = [
        *common_flags,
    ]
    linkflags = [
        f"-target {target}",
        f"-arch {config['arch']}",
        f"-isysroot {sdk_path}",
        version,
    ]

    cmd = [
        "./b2",
        "install",
        "link=static",
        "target-os=iphone",
        "architecture=arm",
        "address-model=64",
        "threading=multi",
        "variant=debug,release",
        f"--prefix={os.path.join(boost_install_path, config['install_path'])}",
        f"toolset=clang",
        f"cflags={' '.join(cflags)}",
        f"cxxflags={' '.join(cxxflags)}",
        f"linkflags={' '.join(linkflags)}",
        f"--sysroot={sdk_path}",
        *required_boost_components,
    ]
    print("cmd: %s" % cmd)
    subprocess.run(cmd, check=True)

    install_root = os.path.join(boost_install_path, config["install_path"])
    lib_dir = os.path.join(install_root, "lib")
    for library in sorted(f for f in os.listdir(lib_dir) if f.endswith(".a")):
        validate_boost_ios_archive_platform(os.path.join(lib_dir, library), expected_platform)


def get_required_boost_component_names():
    prefix = "--with-"
    return [component[len(prefix):] if component.startswith(prefix) else component for component in required_boost_components]


def verify_ios_boost_build_outputs():
    print_step("verify_ios_boost_build_outputs")
    build_info = {
        "boostVersion": boost_version,
        "deploymentTarget": ios_min_version,
        "platforms": {},
    }
    library_sets = []

    for key, val in ios_architectures.items():
        install_root = os.path.join(boost_install_path, val["install_path"])
        include_dir = os.path.join(install_root, "include")
        lib_dir = os.path.join(install_root, "lib")
        if not os.path.isdir(include_dir):
            raise Exception(f"Boost iOS include directory missing: {include_dir}")
        if not os.path.isdir(lib_dir):
            raise Exception(f"Boost iOS library directory missing: {lib_dir}")

        libraries = sorted(f for f in os.listdir(lib_dir) if f.endswith(".a"))
        if not libraries:
            raise Exception(f"No Boost iOS static libraries were produced under: {lib_dir}")

        for component in get_required_boost_component_names():
            if not any(library.startswith(f"libboost_{component}") for library in libraries):
                raise Exception(f"Boost iOS component library missing for {component}: {lib_dir}")

        library_sets.append(set(libraries))
        build_info["platforms"][key] = {
            "sdk": val["sdk"],
            "architecture": val["arch"],
            "installPath": val["install_path"],
            "libraries": libraries,
        }

    base_set = library_sets[0]
    if not all(libraries == base_set for libraries in library_sets):
        raise Exception("Boost iOS device and simulator builds produced different library sets.")

    manifest_path = os.path.join(boost_install_path, "BuildInfo.json")
    with open(manifest_path, "w", encoding="utf-8") as manifest:
        json.dump(build_info, manifest, indent=4)
        manifest.write("\n")

    print("Boost iOS build outputs verified: %s" % manifest_path)





def main():
    check_root_dir()
    shutil.rmtree(boost_root_dir, ignore_errors=True)
    shutil.rmtree(boost_install_path, ignore_errors=True)
    download_from_url()

    if target_platform == "Windows64":
        unzip_files_windows64()
        build_boost_windows64()
    elif target_platform == "Mac":
        unzip_files_mac()
        build_boost_mac()
    elif target_platform == "IOS":
        os.chdir(script_file_dir)
        unzip_files_mac()
        build_ios_b2()
        build_boost_ios_lib(ios_architectures["device"])

        # boost will cache last build, so remove root here.
        os.chdir(script_file_dir)
        shutil.rmtree(boost_root_dir, ignore_errors=True)
        unzip_files_mac()
        build_ios_b2()
        build_boost_ios_lib(ios_architectures["simulator"])

        verify_ios_boost_build_outputs()



if __name__ == "__main__":
    main()
