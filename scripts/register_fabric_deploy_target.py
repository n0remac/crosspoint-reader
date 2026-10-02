"""Publish the default X3/X4 application to the Pi's Fabric dev channel.

Use ``pio run -e default -t fabric-deploy`` after removing any local USB
bootstrap credential header. The registry keeps the uploaded binary immutable.
"""

import configparser
import os
import subprocess

Import("env")  # noqa: F821  -- provided by PlatformIO


def git_value(project_dir, *args):
    return subprocess.check_output(["git", *args], cwd=project_dir, text=True).strip()


def publish(target, source, env):
    project_dir = env["PROJECT_DIR"]
    bootstrap_header = os.path.join(project_dir, "src", "FabricProvisioning.local.h")
    if os.path.exists(bootstrap_header):
        raise RuntimeError("Remove the private USB bootstrap header before publishing")

    config = configparser.ConfigParser()
    config.read(os.path.join(project_dir, "platformio.ini"), encoding="utf-8")
    base_version = config.get("crosspoint", "version")
    branch = git_value(project_dir, "rev-parse", "--abbrev-ref", "HEAD")
    commit = git_value(project_dir, "rev-parse", "--short", "HEAD")
    version = f"{base_version}-dev-{branch}-{commit}"

    image = source[0].get_abspath()
    server = os.environ.get("FABRIC_DEPLOY_SERVER", "http://127.0.0.1:8080")
    token_file = os.path.expanduser("~/.config/fabric/publisher.token")
    reader_token_file = os.path.expanduser("~/.config/fabric/x3.token")
    if os.path.isfile(reader_token_file):
        with open(reader_token_file, "rb") as credential:
            reader_token = credential.read().strip()
        with open(image, "rb") as firmware:
            if reader_token and reader_token in firmware.read():
                raise RuntimeError("Reader credential is embedded in the firmware image")
    command = [
        "fabricctl", "upload", "--server", server, "--source", project_dir,
        "--file", image, "--version", version, "--token-file", token_file,
    ]
    subprocess.run(command, check=True)


if env["PIOENV"] == "default":  # noqa: F821
    env.AddCustomTarget(  # noqa: F821
        name="fabric-deploy",
        dependencies=os.path.join(env["BUILD_DIR"], "firmware.bin"),
        actions=publish,
        title="Publish X3/X4 firmware to Fabric",
        description="Build and upload an immutable development firmware image",
    )
