#include "ssd1306.h"

const char init_oled_type_file[] = "/tmp/.ssd1306_oled_type";

static uint8_t data_buf[1024];
static uint8_t max_lines = 0;
static uint8_t max_columns = 0;
static uint8_t global_x = 0;
static uint8_t global_y = 0;

// Initialize I2C communication with the SSD1306 OLED display
uint8_t ssd1306_init(uint8_t i2c_dev)
{
    uint8_t rc;
    rc = _i2c_init(i2c_dev, SSD1306_I2C_ADDR);
    if (rc > 0)
        return rc;
        
    // test i2c connection
    uint8_t cmd = SSD1306_COMM_CONTROL_BYTE;
    uint8_t result = 0;
    _i2c_write(&cmd, 1);
    _i2c_read(&result, 1);
    if (result == 0)
        return 1;
    
    return 0;
}
// End of ssd1306 usage
uint8_t ssd1306_end()
{
    return _i2c_close();
}
// Turn the OLED display on or off
// onoff: SSD1306_COMM_DISPLAY_OFF = off, SSD1306_COMM_DISPLAY_ON = on
uint8_t ssd1306_oled_onoff(uint8_t onoff)
{
    data_buf[0] = SSD1306_COMM_CONTROL_BYTE;
    if (onoff == 0)
        data_buf[1] = SSD1306_COMM_DISPLAY_OFF;
    else
        data_buf[1] = SSD1306_COMM_DISPLAY_ON;
    
    return _i2c_write(data_buf, 2);
}
// Set the OLED horizon orientation
// orientation: SSD1306_COMM_HORIZ_NORM = horizontal normal, SSD1306_COMM_HORIZ_FLIP = horizontal flip
uint8_t ssd1306_oled_horizontal_flip(uint8_t flip)
{
    data_buf[0] = SSD1306_COMM_CONTROL_BYTE;
    if (flip == 0)
        data_buf[1] = SSD1306_COMM_HORIZ_NORM;
    else
        data_buf[1] = SSD1306_COMM_HORIZ_FLIP;
    
    return _i2c_write(data_buf, 2);
}
// Set the OLED display rotation
// orientation: SSD1306_COMM_DIP_NORM = normal, SSD1306_COMM_DISP_INVERSE = inverted
uint8_t ssd1306_oled_display_flip(uint8_t flip)
{
    data_buf[0] = SSD1306_COMM_CONTROL_BYTE;
    if (flip == 0)
        data_buf[1] = SSD1306_COMM_DISP_NORM;
    else
        data_buf[1] = SSD1306_COMM_DISP_INVERSE;
    
    return _i2c_write(data_buf, 2);
}
/**
 * Set OLED multiplex ratio.
 * @param row: Screen height (e.g., 32 or 64).
 * @return 0 on success.
 */
uint8_t ssd1306_oled_multiplex(uint8_t row)
{
    data_buf[0] = SSD1306_COMM_CONTROL_BYTE;
    data_buf[1] = SSD1306_COMM_MULTIPLEX;   // 0xA8
    data_buf[2] = row - 1;
    return _i2c_write(data_buf, 3);
}

/**
 * Set vertical display offset (0x00 - 0x3F).
 * @param offset: Vertical offset.
 * @return 0 on success.
 */
uint8_t ssd1306_oled_vert_shift(uint8_t offset)
{
    data_buf[0] = SSD1306_COMM_CONTROL_BYTE;
    data_buf[1] = SSD1306_COMM_VERT_OFFSET; // 0xD3
    data_buf[2] = offset;
    return _i2c_write(data_buf, 3);
}

/**
 * Set display clock divide ratio.
 * @param clk: Clock setting (default 0x80).
 * @return 0 on success.
 */
uint8_t ssd1306_oled_set_clock(uint8_t clk)
{
    data_buf[0] = SSD1306_COMM_CONTROL_BYTE;
    data_buf[1] = SSD1306_COMM_CLK_SET;    // 0xD5
    data_buf[2] = clk;
    return _i2c_write(data_buf, 3);
}

/**
 * Set pre-charge period.
 * @param precharge: Value (default 0xF1).
 * @return 0 on success.
 */
uint8_t ssd1306_oled_set_precharge(uint8_t precharge)
{
    data_buf[0] = SSD1306_COMM_CONTROL_BYTE;
    data_buf[1] = SSD1306_COMM_PRECHARGE;  // 0xD9
    data_buf[2] = precharge;
    return _i2c_write(data_buf, 3);
}

/**
 * Set VCOMH deselect level.
 * @param voltage: Value (default 0x40).
 * @return 0 on success.
 */
uint8_t ssd1306_oled_set_deselect(uint8_t voltage)
{
    data_buf[0] = SSD1306_COMM_CONTROL_BYTE;
    data_buf[1] = SSD1306_COMM_DESELECT_LV; // 0xDB
    data_buf[2] = voltage;
    return _i2c_write(data_buf, 3);
}

/**
 * Set COM pins configuration.
 * @param value: Config value (default 0x02).
 * @return 0 on success.
 */
uint8_t ssd1306_oled_set_com_pin(uint8_t value)
{
    data_buf[0] = SSD1306_COMM_CONTROL_BYTE;
    data_buf[1] = SSD1306_COMM_COM_PIN;    // 0xDA
    data_buf[2] = value;
    return _i2c_write(data_buf, 3);
}

/**
 * Set memory addressing mode.
 * @param mode: 0x00=Horizontal, 0x01=Vertical, 0x02=Page.
 * @return 0 on success.
 */
uint8_t ssd1306_oled_set_mem_mode(uint8_t mode)
{
    data_buf[0] = SSD1306_COMM_CONTROL_BYTE;
    data_buf[1] = SSD1306_COMM_MEMORY_MODE; // 0x20
    data_buf[2] = mode;
    return _i2c_write(data_buf, 3);
}

/**
 * Set column address range.
 * @param start: Start column.
 * @param end: End column.
 * @return 0 on success.
 */
uint8_t ssd1306_oled_set_col(uint8_t start, uint8_t end)
{
    data_buf[0] = SSD1306_COMM_CONTROL_BYTE;
    data_buf[1] = SSD1306_COMM_SET_COL_ADDR; // 0x21
    data_buf[2] = start;
    data_buf[3] = end;
    return _i2c_write(data_buf, 4);
}

/**
 * Set page address range.
 * @param start: Start page.
 * @param end: End page.
 * @return 0 on success.
 */
uint8_t ssd1306_oled_set_page(uint8_t start, uint8_t end)
{
    data_buf[0] = SSD1306_COMM_CONTROL_BYTE;
    data_buf[1] = SSD1306_COMM_SET_PAGE_ADDR; // 0x22
    data_buf[2] = start;
    data_buf[3] = end;
    return _i2c_write(data_buf, 4);
}

/**
 * Set screen contrast.
 * @param value: Contrast value (default 0x7F).
 * @return 0 on success.
 */
uint8_t ssd1306_oled_set_constrast(uint8_t value)
{
    data_buf[0] = SSD1306_COMM_CONTROL_BYTE;
    data_buf[1] = SSD1306_COMM_CONTRAST;   // 0x81
    data_buf[2] = value;
    return _i2c_write(data_buf, 3);
}

/**
 * Enable or disable scrolling.
 * @param onoff: 0=disable, non-zero=enable.
 * @return 0 on success.
 */
uint8_t ssd1306_oled_scroll_onoff(uint8_t onoff)
{
    data_buf[0] = SSD1306_COMM_CONTROL_BYTE;
    data_buf[1] = (onoff == 0) ? SSD1306_COMM_DISABLE_SCROLL : SSD1306_COMM_ENABLE_SCROLL;
    return _i2c_write(data_buf, 2);
}
/**
 * Set the X coordinate (column) for the next write operation.
 * @param x: X coordinate (0 to max_columns - 1).
 * @return 0 on success, 1 if out of bounds.
 */
uint8_t ssd1306_oled_set_X(uint8_t x)
{
    if (x >= max_columns)
        return 1;

    global_x = x;
    
    data_buf[0] = SSD1306_COMM_CONTROL_BYTE;
    data_buf[1] = SSD1306_COMM_LOW_COLUMN | (x & 0x0f);
    data_buf[2] = SSD1306_COMM_HIGH_COLUMN | ((x >> 4) & 0x0f);
    
    return _i2c_write(data_buf, 3);
}
/**
 * Set the Y coordinate (page) for the next write operation.
 * @param y: Y coordinate (0 to max_lines / 8 - 1).
 * @return 0 on success, 1 if out of bounds.
 */
uint8_t ssd1306_oled_set_Y(uint8_t y)
{
    if (y >= (max_lines / 8))
        return 1;

    global_y = y;
    
    data_buf[0] = SSD1306_COMM_CONTROL_BYTE;
    data_buf[1] = SSD1306_COMM_PAGE_NUMBER | (y & 0x0f);

    return _i2c_write(data_buf, 2);
}
/**
 * Set both X and Y coordinates for the next write operation.
 * @param x: X coordinate (0 to max_columns - 1).
 * @param y: Y coordinate (0 to max_lines / 8 - 1).
 * @return 0 on success, 1 if out of bounds.
 */
uint8_t ssd1306_oled_set_XY(uint8_t x, uint8_t y)
{
    if (x >= max_columns || y >= (max_lines / 8))
        return 1;

    global_x = x;
    global_y = y;
    
    data_buf[0] = SSD1306_COMM_CONTROL_BYTE;
    data_buf[1] = SSD1306_COMM_PAGE_NUMBER | (y & 0x0f);

    data_buf[2] = SSD1306_COMM_LOW_COLUMN | (x & 0x0f);
    
    data_buf[3] = SSD1306_COMM_HIGH_COLUMN | ((x >> 4) & 0x0f);
    
    return _i2c_write(data_buf, 4);
}
/**
 * Set rotation: 0 or 180 deg.
 * @param degree: 0 or 180
 * @return 0 if OK, 1 if invalid
 */
uint8_t ssd1306_oled_set_rotate(uint8_t degree)
{
    // only degree 0 and 180
    if (degree == 0)
    {
        data_buf[0] = SSD1306_COMM_CONTROL_BYTE;
        data_buf[1] = SSD1306_COMM_HORIZ_FLIP;
        data_buf[2] = SSD1306_COMM_SCAN_REVS;
    
        return _i2c_write(data_buf, 3);
    }
    else if (degree == 180)
    {
        data_buf[0] = SSD1306_COMM_CONTROL_BYTE;
        data_buf[1] = SSD1306_COMM_HORIZ_NORM;
        data_buf[2] = SSD1306_COMM_SCAN_NORM;
    
        return _i2c_write(data_buf, 3);
    }
    else
        return 1;
}
/**
 * Initialize OLED with default config.
 * @param oled_lines: 32, 48, 64
 * @param oled_columns: 64, 128
 * @return 0 if OK, 1 if error
 */
uint8_t ssd1306_oled_default_config(uint8_t oled_lines, uint8_t oled_columns)
{
    if (oled_lines != SSD1306_128_64_LINES && oled_lines != SSD1306_128_32_LINES && SSD1306_64_48_LINES)
        oled_lines = SSD1306_128_64_LINES;
        
    if (oled_columns != SSD1306_128_64_COLUMNS && oled_lines != SSD1306_128_32_COLUMNS && SSD1306_64_48_COLUMNS)
        oled_columns = SSD1306_128_64_COLUMNS;
        
    max_lines = oled_lines;
    max_columns = oled_columns;
    global_x = 0;
    global_y = 0;
    
    if (ssd1306_oled_save_resolution(max_columns, max_lines) != 0)
        return 1;
    
    uint16_t i = 0;
    data_buf[i++] = SSD1306_COMM_CONTROL_BYTE;  //command control byte
    data_buf[i++] = SSD1306_COMM_DISPLAY_OFF;   //display off
    data_buf[i++] = SSD1306_COMM_DISP_NORM;     //Set Normal Display (default)
    data_buf[i++] = SSD1306_COMM_CLK_SET;       //SETDISPLAYCLOCKDIV
    data_buf[i++] = 0x80;                       // the suggested ratio 0x80
    data_buf[i++] = SSD1306_COMM_MULTIPLEX;     //SSD1306_SETMULTIPLEX
    data_buf[i++] = oled_lines - 1;             // height is 32 or 64 (always -1)
    data_buf[i++] = SSD1306_COMM_VERT_OFFSET;   //SETDISPLAYOFFSET
    data_buf[i++] = 0;                          //no offset
    data_buf[i++] = SSD1306_COMM_START_LINE;    //SETSTARTLINE
    data_buf[i++] = SSD1306_COMM_CHARGE_PUMP;   //CHARGEPUMP
    data_buf[i++] = 0x14;                       //turn on charge pump
    data_buf[i++] = SSD1306_COMM_MEMORY_MODE;   //MEMORYMODE
    data_buf[i++] = SSD1306_PAGE_MODE;          // page mode
    data_buf[i++] = SSD1306_COMM_HORIZ_NORM;    //SEGREMAP  Mirror screen horizontally (A0)
    data_buf[i++] = SSD1306_COMM_SCAN_NORM;     //COMSCANDEC Rotate screen vertically (C0)
    data_buf[i++] = SSD1306_COMM_COM_PIN;       //HARDWARE PIN 
    if (oled_lines == 32)
        data_buf[i++] = 0x02;                       // for 32 lines
    else
        data_buf[i++] = 0x12;                       // for 64 lines or 48 lines
    data_buf[i++] = SSD1306_COMM_CONTRAST;      //SETCONTRAST
    data_buf[i++] = 0x7f;                       // default contract value
    data_buf[i++] = SSD1306_COMM_PRECHARGE;     //SETPRECHARGE
    data_buf[i++] = 0xf1;                       // default precharge value
    data_buf[i++] = SSD1306_COMM_DESELECT_LV;   //SETVCOMDETECT                
    data_buf[i++] = 0x40;                       // default deselect value
    data_buf[i++] = SSD1306_COMM_RESUME_RAM;    //DISPLAYALLON_RESUME
    data_buf[i++] = SSD1306_COMM_DISP_NORM;     //NORMALDISPLAY
    data_buf[i++] = SSD1306_COMM_DISPLAY_ON;    //DISPLAY ON             
    data_buf[i++] = SSD1306_COMM_DISABLE_SCROLL;//Stop scroll
    
    return _i2c_write(data_buf, i);
}
/**
 * Write a text line to OLED.
 * @param size: Font size ID (5x7, 6x10, 8x8)
 * @param ptr: Pointer to null-terminated string
 * @return 0 if OK, 1 if error
 */
uint8_t ssd1306_oled_write_line(uint8_t size, char* ptr)
{
    uint16_t i = 0;
    uint16_t index = 0;
    uint8_t* font_table = 0;
    uint8_t font_table_width = 0;
    
    if (ptr == 0)
        return 1;
    
    if (size == SSD1306_FONT_5X7) // 5x7
    {
        font_table = (uint8_t*)font5x7;
        font_table_width = 5;
    }
    else if (size == SSD1306_FONT_8X8) // 8x8
    {
        font_table = (uint8_t*)font8x8;
        font_table_width = 8;
    }
    else if (size == SSD1306_FONT_6X10) // 6x10
    {
        font_table = (uint8_t*)font6x10;
        font_table_width = 6;
    }
    else
        return 1; // unsupported font size
    
    data_buf[i++] = SSD1306_DATA_CONTROL_BYTE;
    
    // font table range in ascii table is from 0x20(space) to 0x7e(~)
    while (ptr[index] != 0 && i <= 1024)
    {
        if ((ptr[index] < ' ') || (ptr[index] > '~'))
            return 1;

        uint8_t* font_ptr = &font_table[(ptr[index] - 0x20) * font_table_width];
        uint8_t j = 0;
        for (j = 0; j < font_table_width; j++)
        {
            data_buf[i++] = font_ptr[j];
            if (i > 1024)
                return 1;
        }
        // insert 1 col space for small font size)
        if (size == SSD1306_FONT_5X7)
            data_buf[i++] = 0x00;
        index++;
    }
    
    return _i2c_write(data_buf, i);
}
/**
 * Write a string to OLED with auto line break on "\n".
 * @param size: Font size ID (5x7, 6x10, 8x8)
 * @param ptr: Null-terminated string with optional "\n"
 * @return 0 if OK, >0 if error
 */
uint8_t ssd1306_oled_write_string(uint8_t size, char* ptr)
{
    uint8_t rc = 0;
    
    if (ptr == 0)
        return 1;
    
    char* line = 0;
    char* cr = 0;
    char buf[20];
    
    line = ptr;
    do {
        memset(buf, 0, 20);
        cr = strstr(line, "\\n");
        if (cr != NULL)
        {
            strncpy(buf, line, cr - line);
        }
        else
        {
            strcpy(buf, line);
        }
        
        // set cursor position
        ssd1306_oled_set_XY(global_x, global_y);
        rc += ssd1306_oled_write_line(size, buf);
        
        if (cr != NULL)
        {
            line = &cr[2];
            global_x = 0;
            global_y++;
            if (global_y >= (max_lines / 8))
                global_y = 0;
        }
        else
            line = NULL;
                
    }while (line != NULL);
    
    return rc;
}
/**
 * Clear a specific line (page) on OLED.
 * @param row: Page index (0..(max_lines/8 - 1))
 * @return 0 if OK, 1 if out of range
 */
uint8_t ssd1306_oled_clear_line(uint8_t row)
{
    uint8_t i;
    if (row >= (max_lines / 8))
        return 1;
        
    ssd1306_oled_set_XY(0, row);
    data_buf[0] = SSD1306_DATA_CONTROL_BYTE;
    for (i = 0; i < max_columns; i++)
        data_buf[i+1] = 0x00;
        
    return _i2c_write(data_buf, 1 + max_columns);
}

/**
 * Clear the entire OLED screen.
 * @return 0 if OK, >0 if error
 */
uint8_t ssd1306_oled_clear_screen()
{
    uint8_t rc = 0;
    uint8_t i;
    
    for (i = 0; i < (max_lines / 8); i++)
    {
        rc += ssd1306_oled_clear_line(i);
    }
    
    return rc;
}
/**
 * Save OLED resolution to a file for persistent config.
 * @param column: OLED width (columns)
 * @param row: OLED height (rows)
 * @return 0 if OK, 1 if failed to create file
 */
uint8_t ssd1306_oled_save_resolution(uint8_t column, uint8_t row)
{
    FILE* fp;
    
    fp = fopen(init_oled_type_file, "w");
    
    if (fp == NULL)
    {
        // file create failed
        return 1;
    }
    
    fprintf(fp, "%hhux%hhu", column, row);
    fclose(fp);
    
    return 0;
}
/**
 * Load OLED resolution from a file to initialize max_columns and max_lines.
 * @return 0 if OK, 1 if file not found
 */
uint8_t ssd1306_oled_load_resolution()
{
    FILE* fp;
    
    fp = fopen(init_oled_type_file, "r");
    
    if (fp == NULL)
    {
        // file not exists
        return 1;
    }
    
    // file exists
    fscanf(fp, "%hhux%hhu", &max_columns, &max_lines);
    fclose(fp);
    
    return 0;
}