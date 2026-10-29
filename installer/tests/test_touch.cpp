// The installer's touch rules: when a finger taps, when it drags, and which part of a row it can hit.
#include "../source/touch.hpp"
#include <cstdio>
using namespace touch;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main() {
    // A tap that wobbles a little stays a tap and never moves the page.
    Finger f;
    CHECK(f.down(100, 200).scroll == 0);
    CHECK(f.down(104, 205).scroll == 0);
    Finger::Step s = f.up();
    CHECK(s.tapped && s.x == 104 && s.y == 205);
    CHECK(!f.up().tapped);  // no finger, no tap

    // A drag moves the page once past the slop, by the whole way from where it started, then follows the finger.
    CHECK(f.down(100, 400).scroll == 0);
    CHECK(f.down(100, 390).scroll == 0);
    CHECK(f.down(100, 380).scroll == 20);
    CHECK(f.down(100, 370).scroll == 10);
    CHECK(f.down(100, 375).scroll == -5);
    CHECK(!f.up().tapped);

    // Sideways travel counts toward the slop but never scrolls.
    f.down(100, 300);
    CHECK(f.down(120, 300).scroll == 0);
    CHECK(!f.up().tapped);

    // A new touch starts fresh after a drag.
    f.down(10, 10);
    CHECK(f.up().tapped);

    // Edges: the left and top edges are inside, the right and bottom ones are not.
    const Box b{10, 20, 30, 40};
    CHECK(inside(b, 10, 20) && inside(b, 39, 59) && !inside(b, 40, 30) && !inside(b, 20, 60) && !inside(b, 9, 30));

    // Rows: whole rows keep their box; a row half under the footer is cut to what shows; less than half is out.
    Box hit;
    CHECK(visiblePart({0, 100, 50, 46}, 86, 652, hit) && hit.y == 100 && hit.h == 46);
    CHECK(visiblePart({0, 629, 50, 46}, 86, 652, hit) && hit.y == 629 && hit.h == 23);
    CHECK(!visiblePart({0, 630, 50, 46}, 86, 652, hit));
    CHECK(visiblePart({0, 63, 50, 46}, 86, 652, hit) && hit.y == 86 && hit.h == 23);
    CHECK(!visiblePart({0, 62, 50, 46}, 86, 652, hit));
    CHECK(!visiblePart({0, 700, 50, 46}, 86, 652, hit));

    if (failures) { std::printf("%d touch test(s) failed\n", failures); return 1; }
    std::printf("All touch tests passed!\n");
    return 0;
}
