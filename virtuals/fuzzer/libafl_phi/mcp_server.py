from __future__ import annotations

import os
import re
import subprocess
from typing import List

from mcp.server.fastmcp import FastMCP

# URL used by this MCP to reach the model backend.
# Can be overridden with the OPENAI_URL environment variable.
OPENAI_URL = os.environ.get("OPENAI_URL", "https://api.openai.com/v1/chat/completions")

mcp = FastMCP("optifuzz-mcp")


@mcp.tool()
def list_files(path: str = ".") -> List[str]:
    """List files in a directory on the local filesystem."""
    try:
        return sorted(os.listdir(path))
    except OSError as exc:
        return [f"Error listing '{path}': {exc}"]

@mcp.tool()
def read_file(path: str) -> str:
    """Read the contents of a file on the local filesystem."""
    try:
        with open(path, "r") as file:
            return file.read()
    except OSError as exc:
        return f"Error reading '{path}': {exc}"


@mcp.tool()
def set_sdf_world_coords(sdf_path: str, lat: float, lon: float, alt: float) -> str:
    """
    Parse an SDF world file, update latitude_deg, longitude_deg, and elevation
    in the spherical_coordinates block, and write the file back in place.

    Args:
        sdf_path: Path to the .sdf file (e.g. SITL_Models/Gazebo/worlds/r1_rover_runway.sdf)
        lat: New latitude in degrees
        lon: New longitude in degrees
        alt: New altitude/elevation in meters (written to <elevation>)

    Returns:
        A message indicating success or an error description.
    """
    if not os.path.isfile(sdf_path):
        return f"Error: file not found: {sdf_path}"

    try:
        with open(sdf_path, "r") as f:
            content = f.read()
    except OSError as e:
        return f"Error reading '{sdf_path}': {e}"

    # Replace first occurrence of each tag so we only change the active world block
    lat_pat = re.compile(r"<latitude_deg>\s*[^<]*\s*</latitude_deg>")
    lon_pat = re.compile(r"<longitude_deg>\s*[^<]*\s*</longitude_deg>")
    elev_pat = re.compile(r"<elevation>\s*[^<]*\s*</elevation>")

    if not lat_pat.search(content):
        return f"Error: no <latitude_deg> found in {sdf_path}"
    if not lon_pat.search(content):
        return f"Error: no <longitude_deg> found in {sdf_path}"
    if not elev_pat.search(content):
        return f"Error: no <elevation> found in {sdf_path}"

    content = lat_pat.sub(f"<latitude_deg>{lat}</latitude_deg>", content, count=1)
    content = lon_pat.sub(f"<longitude_deg>{lon}</longitude_deg>", content, count=1)
    content = elev_pat.sub(f"<elevation>{alt}</elevation>", content, count=1)

    try:
        with open(sdf_path, "w") as f:
            f.write(content)
    except OSError as e:
        return f"Error writing '{sdf_path}': {e}"

    return f"Updated {sdf_path}: lat={lat}, lon={lon}, alt={alt}"

@mcp.tool()
def run_fuzzing_campaign(campaign_name: str) -> str:
    """
    Run a fuzzing campaign.
    """
    # run the ./targets/release/baby_fuzzer binary
    subprocess.run(["./target/release/baby_fuzzer"])
    return f"Fuzzing campaign {campaign_name} started."

if __name__ == "__main__":
    try:
        mcp.run()
    except KeyboardInterrupt:
        print("Server stopped by user.")
    except Exception as e:
        print(f"Server error: {e}")
        raise
    finally:
        print("Server stopped.")