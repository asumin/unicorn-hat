import client as u
from random import randint

u.connect()
u.clear()

while True:
    x = randint(0, 3)
    y = randint(0, 15)
    w = randint(0, 255)
    r = randint(0, 255)
    g = randint(0, 255)
    b = randint(0, 255)
    u.set_pixel(x, y, w, r, g, b)
    u.show()
