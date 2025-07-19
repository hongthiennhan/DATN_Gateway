#include "main.h"

#define CHIP_NAME "/dev/gpiochip0"
#define GPIO_LINE 17

int main() {
    struct gpiod_chip *chip;
    struct gpiod_line *line;
    int rv;

    chip = gpiod_chip_open(CHIP_NAME);
    if (!chip) {
        return 1;
    }
    line = gpiod_chip_get_line(chip, GPIO_LINE);
    if (!line) {
        gpiod_chip_close(chip);
        return 1;
    }
    rv = gpiod_line_request_output(line, "blink", 0);
    if (rv < 0) {
        gpiod_chip_close(chip);
        return 1;
    }

    for (;;) {
        gpiod_line_set_value(line, 1);
        usleep(500000);
        gpiod_line_set_value(line, 0);
        usleep(500000);
    }
    gpiod_line_release(line);
    gpiod_chip_close(chip);
    return 0;
}