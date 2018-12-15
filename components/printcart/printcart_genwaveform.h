
/*
These are routines that manage the waveforms for firing all the nozzles of the cartridge once,
to essentially fire a 'row' of ink drops. You use this by allocating an uint8_t-sized buffer of
PRINTCART_NOZDATA_SZ to contain the nozzle data. Clear it by setting all elements to 0, then 
use printcart_fire_nozzle_color or printcart_fire_nozzle_mono to enable firing a nozzle. 
Finally, feed the nozzle data to printcart_generate_waveform to generate the actual waveform that
needs to be sent to the cartridge.
*/

//Size of the nozzle data, in bytes
#define PRINTCART_NOZDATA_SZ (14*3)

//Colors, for printcart_fire_nozzle_color
#define PRINTCART_COLOR_C 0
#define PRINTCART_COLOR_M 1
#define PRINTCART_COLOR_Y 2

/*
In the nozzle data array `l`, this enables the `p`'th nozzle from the top of the row of nozzles with color 
`color` to fire.
*/
void printcart_fire_nozzle_color(uint8_t *l, int p, int color);

/*
In the nozzle data array `l` , this function sets the enable bit for the `p`'th nozzle from the top 
of the inkjet nozzles in row `row`. The black cartridge has two rows, the 2nd one is slightly offset in the X 
direction and interleaved with the 1st (offset by half a nozzle). Note that the 2 first and last nozzles 
of each 168-nozzle row are not connected (giving a total of 324 nozzles in the combined two rows).
*/
void printcart_fire_nozzle_black(uint8_t *l, int p, int row);


/*
Use the nozzle data in `nozdata` combined with the waveform template `tp` which has a length of `l` 16-bit 
elements, generate the waveform to send to the printer cartridge and put it in the buffer `w`. Returns the 
amount of elements used in buffer `w`. This return value will always be the same given a certain template; 
make sure `w` is sized accordingly.

Note that in the ESP32 implementation, this writes w in a fashion usable for sending through the parallel
I2S peripheral; if you use this on another controller, you may need to change the C code (specifically
the write_signals function).
*/
int printcart_generate_waveform(uint16_t *w, const uint16_t *tp, const uint8_t *nozdata, int l);
