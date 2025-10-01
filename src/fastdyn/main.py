'''
Main file is responsible for kicking the qemu command.
'''
import logging
import argparse

from dotenv import load_dotenv


from . import parse_config  #Parse the config
from . import fastdyn_log

log = logging.getLogger(__name__)
fastdyn_log.setLogConfig()


#build qemu command
def build_qemu_cmd(dev_config):
    cmd = {


    }

    return cmd







#This function is responsible for running the qemu command based on the inputs
def run_qemu(config, out_path):




    return None



def main():
    # Load variables from .env
    load_dotenv()
    parser = argparse.ArgumentParser(
        description="Parse, display, and generate configurations from a FastDyn TOML file.",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument(
        "-c", "--config-path",
        required=True,
        help="Path to the TOML configuration file."
    )
    parser.add_argument(
        "-m","--map-file",
        default=None,
        help="Path to the symbol map file.")

    parser.add_argument(
        "-o", "--output",
        default='out',
        metavar="OUTPUT_DIR",
        help="Directory to place the generated files (default: './out')."
    )

    args = parser.parse_args()

    config = parse_config.Fastdyn_Config()  #generate the object for the config

    log.info(f"Parsing Config file: {args.config_path}")
    config.add_device_config(args.config_path)


    run_qemu(
        config=config,
        out_path=args.output
    )


if __name__ == "__main__":
    main()