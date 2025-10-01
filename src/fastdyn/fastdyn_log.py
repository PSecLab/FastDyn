import logging
import logging.config
from os import path

LOG_CONFIG_NAME = 'logging.cfg'
DEFAULT_LOG_CONFIG = path.join(path.dirname(__file__), 'logging.cfg')
FASTDYN_LOGGER = "FASTDYN_LOG"

class ColoredFormatter(logging.Formatter):
    COLORS = {
        'DEBUG': '\033[37m',    # White
        'INFO': '\033[32m',     # Green
        'WARNING': '\033[33m',  # Yellow
        'ERROR': '\033[31m',    # Red
        'CRITICAL': '\033[41m', # Red background
    }
    RESET = '\033[0m'

    def format(self, record):
        color = self.COLORS.get(record.levelname, self.RESET)
        record.msg = f"{color}{record.msg}{self.RESET}"
        return super().format(record)

def getFastdynLogger():
    return logging.getLogger(FASTDYN_LOGGER)

def setLogConfig():
    """Configures logging using a local or default config file with colors."""
    if path.isfile(LOG_CONFIG_NAME):
        logging.config.fileConfig(fname=LOG_CONFIG_NAME, disable_existing_loggers=True)
    else:
        logging.config.fileConfig(fname=DEFAULT_LOG_CONFIG, disable_existing_loggers=False)

    # Attach colored formatter to all StreamHandlers
    for logger_name in logging.root.manager.loggerDict:
        logger = logging.getLogger(logger_name)
        for handler in logger.handlers:
            if isinstance(handler, logging.StreamHandler):
                handler.setFormatter(ColoredFormatter('%(name)s|%(levelname)s|  %(message)s'))
