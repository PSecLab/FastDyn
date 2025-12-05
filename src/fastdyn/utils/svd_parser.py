import logging
import os
from .. import fastdyn_log as fastdyn_log_conf
import subprocess

log = logging.getLogger(__name__)
fastdyn_log = fastdyn_log_conf.getFastdynLogger()

from cmsis_svd.parser import SVDParser

def discover_svd_files():
    repo_dir = "cmsis-svd-data"; repo_url = "https://github.com/cmsis-svd/cmsis-svd-data.git"
    if not os.path.isdir(repo_dir):
        fastdyn_log.info(f"SVD data directory '{repo_dir}' not found.")
        try:
            if input("   Would you like to clone it now? (y/n): ").lower() != 'y': fastdyn_log.info("Aborting."); sys.exit(0)
            fastdyn_log.info(f"Cloning SVD data from {repo_url}..."); subprocess.run(["git", "clone", "--depth", "1", repo_url, repo_dir], check=True, capture_output=True); fastdyn_log.info("Clone successful.")
        except (FileNotFoundError, subprocess.CalledProcessError):
            fastdyn_log.error("Failed to clone repository. Please install Git or clone it manually."); sys.exit(1)
    svd_map = {}
    data_path = os.path.join(repo_dir, "data")
    for root, _, files in os.walk(data_path):
        for filename in files:
            if filename.endswith('.svd'):
                device_name = os.path.splitext(filename)[0]
                if device_name in svd_map: fastdyn_log.debug(f"Duplicate device name '{device_name}'.")
                svd_map[device_name] = os.path.join(root, filename)
    fastdyn_log.info(f"Automatically discovered {len(svd_map)} SVD files.")
    return svd_map
