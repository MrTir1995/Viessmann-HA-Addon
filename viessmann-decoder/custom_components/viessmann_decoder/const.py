"""Constants for the decoder integration."""

from datetime import timedelta

DOMAIN = "viessmann_decoder"
CONF_URL = "url"
UPDATE_INTERVAL = timedelta(seconds=10)
REQUEST_TIMEOUT = 10
MAX_RESPONSE_BYTES = 65536
PROTOCOL_NAMES = {0: "VBUS", 1: "KW", 2: "P300", 3: "KM", 4: "KM remote"}
