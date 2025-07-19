#include "linux_i2c.h"

static int file_i2c = 0; // File descriptor for opened /dev/i2c-X

/**
 * Initialize I2C bus and set slave address.
 * @param i2c: I2C bus number (e.g., 1 for /dev/i2c-1)
 * @param dev_addr: I2C device address
 * @return 0 if OK, 1 if error
 */
uint8_t _i2c_init(int i2c, int dev_addr)
{
    if (file_i2c == 0)
    {
        char filename[32];
        sprintf(filename, "/dev/i2c-%d", i2c);
        file_i2c = open(filename, O_RDWR);
        if (file_i2c < 0)
        {
            file_i2c = 0;
            return 1;
        }
        if (ioctl(file_i2c, I2C_SLAVE, dev_addr) < 0)
        {
            close(file_i2c);
            file_i2c = 0;
            return 1;
        }
        return 0;
    }
    // Already initialized
    return 0;
}

/**
 * Close the opened I2C bus.
 * @return 0 if closed, 1 if not opened
 */
uint8_t _i2c_close()
{
    if (file_i2c != 0)
    {
        close(file_i2c);
        file_i2c = 0;
        return 0;
    }
    return 1;
}

/**
 * Write data buffer to I2C device.
 * @param ptr: Pointer to data buffer
 * @param len: Length of data
 * @return 0 if OK, 1 if error
 */
uint8_t _i2c_write(uint8_t* ptr, int16_t len)
{
    if (file_i2c == 0 || ptr == 0 || len <= 0)
        return 1;

    write(file_i2c, ptr, len);
    return 0;
}

/**
 * Read data buffer from I2C device.
 * @param ptr: Pointer to buffer to store data
 * @param len: Number of bytes to read
 * @return 0 if OK, 1 if error
 */
uint8_t _i2c_read(uint8_t *ptr, int16_t len)
{
    if (file_i2c == 0 || ptr == 0 || len <= 0)
        return 1;

    read(file_i2c, ptr, len);
    return 0;
}
