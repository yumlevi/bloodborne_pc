"""Adapt BBSC's EU ss.info to the retail US/Asia region field names.

BBSC's pinned game server builds one EU response, even when the game requests
/bb-us/ss.info or /bb-asia/ss.info. The game selects region-numbered fields,
so an HTTP 200 with the wrong field names is still an unusable status reply.
"""

import base64
import re


REGIONS = {
    "bb-us": ("1", (2, 3, 18, 21)),
    "bb-asia": ("4", (10, 11, 12, 19)),
}


def _replace_languages(xml, tag, languages):
    pattern = rf"<{tag}>(.*?)</{tag}>"

    def replace(match):
        values = re.findall(r"<lang\d+>(.*?)</lang\d+>", match.group(1), re.DOTALL)
        if not values or len(set(values)) != 1:
            raise ValueError(f"BBSC {tag} language block changed")
        lines = "\n".join(f"<lang{number}>{values[0]}</lang{number}>" for number in languages)
        return f"<{tag}>\n{lines}\n</{tag}>"

    result, count = re.subn(pattern, replace, xml, flags=re.DOTALL)
    if count != 1:
        raise ValueError(f"BBSC {tag} block missing or duplicated")
    return result


def adapt_region_ss_info(path, encoded):
    """Return region-correct base64 XML; preserve BBSC's EU response verbatim."""
    region = path.split("/", 2)[1] if path.startswith("/") else ""
    setting = REGIONS.get(region)
    if setting is None:
        return encoded
    suffix, languages = setting
    xml = base64.b64decode(encoded, validate=True).decode("utf-8")
    if not all(f"<{tag}2>" in xml for tag in
               ("msg", "BetaTestWebUrl", "gameurl", "ReloadServerStatusInfoInterval")):
        raise ValueError("BBSC EU ss.info template changed")
    xml = re.sub(r"(<\/?)([A-Za-z][A-Za-z0-9_]*)2(?=>)",
                 lambda match: f"{match.group(1)}{match.group(2)}{suffix}", xml)
    for tag in (f"msg{suffix}", f"BetaTestWebUrl{suffix}"):
        xml = _replace_languages(xml, tag, languages)
    return base64.b64encode(xml.encode("utf-8"))
