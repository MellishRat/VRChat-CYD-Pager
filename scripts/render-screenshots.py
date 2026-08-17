"""Render clean README screenshots of the 320x240 CYD interface."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

S = 2
W, H = 320, 240
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "screenshots"
OUT.mkdir(exist_ok=True)

C = {
    "bg": "#08202a", "panel": "#102c36", "panel2": "#18343d",
    "border": "#3b7d7e", "white": "#ffffff", "grey": "#858585",
    "light": "#c6c6c6", "blue": "#00a2ff", "lightblue": "#4bdfff",
    "green": "#00d45a", "red": "#f43b3b", "orange": "#ff9d18",
    "yellow": "#ffe51c", "purple": "#bd40ff", "button": "#21445b",
    "black": "#000000",
}

FONT_REGULAR = Path("C:/Windows/Fonts/consola.ttf")
FONT_BOLD = Path("C:/Windows/Fonts/consolab.ttf")


class Screen:
    def __init__(self):
        self.image = Image.new("RGB", (W * S, H * S), C["black"])
        self.draw = ImageDraw.Draw(self.image)

    def font(self, size, bold=True):
        path = FONT_BOLD if bold else FONT_REGULAR
        return ImageFont.truetype(str(path), size * S)

    def rect(self, box, colour, radius=0, outline=None):
        scaled = tuple(v * S for v in box)
        self.draw.rounded_rectangle(scaled, radius=radius * S, fill=colour,
                                    outline=outline, width=S if outline else 1)

    def text(self, value, x, y, size=10, colour=None, anchor="lm", bold=True):
        self.draw.text((x * S, y * S), value, font=self.font(size, bold),
                       fill=colour or C["white"], anchor=anchor)

    def line(self, x1, y1, x2, y2, colour=None):
        self.draw.line((x1*S, y1*S, x2*S, y2*S), fill=colour or C["border"], width=S)

    def button(self, x, y, w, h, label, colour=None, text_colour=None, size=9):
        self.rect((x, y, x+w, y+h), colour or C["button"], 5, C["border"])
        self.text(label, x+w/2, y+h/2, size, text_colour or C["white"], "mm")

    def top(self, title, badge="LIVE"):
        self.rect((0, 0, W, H), C["bg"])
        self.text(title, 12, 16, 14, C["lightblue"])
        badge_colour = C["green"] if badge == "LIVE" else C["orange"]
        self.rect((260, 7, 310, 25), badge_colour, 4)
        self.text(badge, 285, 16, 8, C["black"], "mm")
        self.line(8, 30, 312, 30)

    def back(self):
        self.button(5, 4, 58, 22, "< BACK", size=7)

    def sub_top(self, title, colour=None):
        self.rect((0, 0, W, H), C["bg"])
        self.text(title, 160, 15, 11, colour or C["lightblue"], "mm")
        self.back()

    def rows(self, items, start=40):
        for i, (title, detail, *colour) in enumerate(items):
            y = start + i * 38
            self.rect((8, y, 312, y+33), C["panel2"] if i % 2 else C["panel"], 4)
            self.text(title, 15, y+10, 10, colour[0] if colour else C["white"])
            self.text(detail, 15, y+24, 8, C["grey"], bold=False)

    def save(self, name):
        self.image.save(OUT / f"{name}.png", optimize=True)


def boot():
    s = Screen()
    s.text("VRCHAT", 160, 72, 28, C["lightblue"], "mm")
    s.text("PAGER", 160, 113, 28, C["white"], "mm")
    s.text("STABLE PIPELINE", 160, 158, 12, C["grey"], "mm")
    return s


def wifi():
    s = Screen(); s.top("SELECT WI-FI", "SCAN")
    s.text("Choose a 2.4 GHz network", 12, 43, 9, C["grey"], bold=False)
    s.rows([("MellishNet", "Strong signal - secured", C["white"]),
            ("Home Wi-Fi", "Good signal - secured"),
            ("Guest Network", "Weak signal - open"),
            ("OTHER NETWORK", "Enter network name manually", C["lightblue"])], 58)
    return s


def login():
    s = Screen(); s.top("VRCHAT LOGIN", "READY")
    s.text("Sign in directly on this device", 15, 42, 9, C["grey"], bold=False)
    s.button(15, 55, 290, 42, "USERNAME", C["panel2"], C["light"])
    s.button(15, 109, 290, 42, "PASSWORD", C["panel2"], C["light"])
    s.button(15, 172, 140, 48, "CLEAR")
    s.button(165, 172, 140, 48, "LOGIN", C["blue"])
    return s


def two_factor():
    s = Screen(); s.top("TWO-FACTOR CODE", "VERIFY")
    s.text("Enter the code from your authenticator", 160, 45, 8, C["grey"], "mm", False)
    for i, n in enumerate("123456789"):
        s.button(54+(i%3)*72, 74+(i//3)*43, 62, 36, n, C["panel2"], size=13)
    s.button(18, 205, 84, 30, "DELETE", size=8)
    s.button(118, 205, 84, 30, "0", C["panel2"], size=12)
    s.button(218, 205, 84, 30, "VERIFY", C["green"], C["black"], 8)
    return s


def home():
    s = Screen(); s.top("VRCHAT PAGER")
    s.text("MellishRat", 12, 42, 9, C["grey"], bold=False)
    s.text("CONNECTED", 160, 72, 20, C["green"], "mm")
    s.text("Pipeline is listening", 160, 98, 9, C["grey"], "mm", False)
    s.button(10, 130, 145, 43, "NOTIFICATIONS", C["blue"])
    s.button(165, 130, 145, 43, "EVENTS", size=10)
    s.button(10, 184, 145, 43, "GROUPS", size=10)
    s.button(165, 184, 145, 43, "ADVANCED", size=10)
    return s


def notifications():
    s = Screen(); s.sub_top("NOTIFICATIONS")
    s.rows([("Friend request", "PixelFox - 2 min ago", C["green"]),
            ("Invite", "Nova invited you to join", C["blue"]),
            ("Boop", "Mochi sent you a boop", C["purple"]),
            ("System", "VRChat notification", C["yellow"]),
            ("Invite request", "Luna - 18 min ago", C["blue"])], 37)
    return s


def events():
    s = Screen(); s.sub_top("EVENT TIMELINE")
    s.rows([("PixelFox", "Came online", C["green"]),
            ("Friend", "Went offline", C["grey"]),
            ("Nova", "Location private", C["lightblue"]),
            ("Mochi", "Active", C["green"]),
            ("Luna", "At The Black Cat", C["white"])], 37)
    return s


def groups():
    s = Screen(); s.sub_top("GROUP ACTIVITY", C["purple"])
    s.rows([("Mellish Community", "Group announcement", C["yellow"]),
            ("VR Dance Club", "New group instance", C["lightblue"]),
            ("Creators Hub", "Member joined", C["green"]),
            ("Weekend Meetup", "Event updated", C["orange"])], 37)
    return s


def detail():
    s = Screen(); s.sub_top("DETAILS")
    s.text("FRIEND REQUEST", 12, 45, 9, C["green"])
    s.text("PixelFox", 12, 67, 16); s.line(12, 82, 308, 82)
    s.text("Sent you a friend request.", 12, 103, 10, C["light"], bold=False)
    s.text("Received 2 minutes ago", 12, 132, 8, C["grey"], bold=False)
    s.text("Tap BACK to return", 12, 220, 8, C["grey"], bold=False)
    return s


def advanced():
    s = Screen(); s.sub_top("INFO / ADVANCED")
    s.text("SESSION", 12, 47, 10, C["lightblue"])
    s.text("Signed in as MellishRat", 12, 66, 9)
    s.text("Pipeline connections: 1", 12, 87, 8, C["grey"], bold=False)
    s.text("Last message: 4 sec ago", 12, 104, 8, C["grey"], bold=False)
    s.button(7, 163, 145, 32, "RECONNECT", size=8)
    s.button(168, 163, 145, 32, "CHANGE WI-FI", size=8)
    s.button(7, 202, 145, 32, "TIME ZONE", size=8)
    s.button(168, 202, 145, 32, "LOG OUT", C["red"], size=8)
    return s


def timezone():
    s = Screen(); s.sub_top("TIME SETTINGS")
    for i, label in enumerate(("* UNITED KINGDOM", "UTC", "US EASTERN", "US CENTRAL")):
        s.button(12, 55+i*36, 296, 31, label, C["blue"] if i == 0 else C["panel2"])
    s.button(50, 207, 100, 28, "PREVIOUS", size=8)
    s.button(170, 207, 100, 28, "NEXT", size=8)
    return s


SCREENS = {
    "01-boot": boot, "02-wifi-scan": wifi, "03-vrchat-login": login,
    "04-two-factor-authentication": two_factor, "05-home": home,
    "06-notifications": notifications, "07-friend-events": events,
    "08-group-events": groups, "09-notification-detail": detail,
    "10-advanced": advanced, "11-time-zone": timezone,
}

for filename, renderer in SCREENS.items():
    renderer().save(filename)

print(f"Rendered {len(SCREENS)} screenshots to {OUT}")
