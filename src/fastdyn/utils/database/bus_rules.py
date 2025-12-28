BUS_RULES = {
    "i2c": {
        "required": ["address"],
        "optional": ["speed", "addr_width"],
    },
    "spi": {
        "required": ["cs_id"],
        "optional": ["mode", "max_hz"],
    },
}

def infer_bus_from_parent(dev_name: str) -> str | None:
    n = dev_name.lower()
    if "i2c" in n:
        return "i2c"
    if "spi" in n:
        return "spi"
    return None