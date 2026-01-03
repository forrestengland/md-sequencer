#include "md.h"
#include "z80.h"
#include "controller.h"
#include "z80driver.h"
#include "amen_unsigned.h"

#include <stdint.h>

#define stopCommand_addr 0x0100
#define playCommand_addr 0x00FF
#define sampleStart_addr 0x00103  // 2 bytes
#define sampleLength_addr 0x0101 // 2 bytes
#define accent_addr 0x00FE
#define speed_addr 0x0FD
#define sampleMax 2 // maximum sample index

int gateseq[16] = {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
int accseq[16] = {1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0};
int speedseq[16] = {20,21,22,30,29,28,10,12,14,15,13,11,9,8,7,6};
int seqpos = 0;
int framemod = 10;
int column = 0;
int oldcolumn = 0;
#define COLUMN_COUNT 3;

// Define key SRAM memory addresses as volatile pointers
// Volatile is crucial as the hardware might change values outside the C program's control
#define SRAM_START_ADDR ((volatile uint8_t*)0x200001)
#define SRAM_END_ADDR ((volatile uint8_t*)0x20FFFF)
#define SRAM_LOCK_ADDR  ((volatile uint8_t*)0xA130F1)

// Define a simple structure for game save data
typedef struct {
  uint16_t magic;
  uint8_t sequence[16];
  uint8_t accent[16];
  uint8_t speed[16];
  uint8_t  checksum;      // Simple checksum for data integrity
  uint8_t  padding;       // Padding to ensure alignment if needed, although 8-bit access is standard
} GameSaveData;

// --- Function Prototypes ---
void save_game_to_sram(const GameSaveData* data);
uint8_t load_game_from_sram(GameSaveData* data);
uint8_t calculate_checksum(const GameSaveData* data);
void unlock_sram(void);
void lock_sram(void);


// --- Function Implementations ---

/**
 * Unlock SRAM for writing.
 */
void unlock_sram(void) {
    // Write 1 to the SRAM lock address to enable writing
    *SRAM_LOCK_ADDR = 1;
}

/**
 * Lock SRAM to prevent accidental writes.
 */
void lock_sram(void) {
    // Write 0 to the SRAM lock address to disable writing
    *SRAM_LOCK_ADDR = 0;
}

/**
 * Calculate a simple checksum for the save data.
 */
uint8_t calculate_checksum(const GameSaveData* data) {
    uint8_t sum = 0;
    const uint8_t* byte_ptr = (const uint8_t*)data;
    // Iterate over all bytes except the checksum itself (last byte before padding)
    for (long unsigned int i = 0; i < sizeof(GameSaveData) - sizeof(uint8_t); i++) {
        sum += byte_ptr[i];
    }
    return sum;
}

/**
 * Save game data to the Sega Genesis SRAM.
 */
void save_game_to_sram(const GameSaveData* data) {
    // In bare-metal C, you need to disable interrupts before accessing memory-mapped I/O
    // This is system-specific, usually involving assembly code or specific CPU registers.
    // Assuming you have a function `disable_interrupts()` and `enable_interrupts()`

    // disable_interrupts(); 
    unlock_sram();

    // The Sega Genesis uses an odd 8-bit addressing scheme for SRAM
    // This means we must write to every *other* physical address.
    // In C, we can iterate over our structure byte by byte and manually
    // write to the correct addresses, offsetting by 2 bytes in memory for each logical byte of data.

    const uint8_t* src = (const uint8_t*)data;
    volatile uint8_t* dest = SRAM_START_ADDR;
    
    for (long unsigned int i = 0; i < sizeof(GameSaveData); i++) {
        // Write the byte from the source structure to the SRAM destination
        *dest = src[i];
        // Move to the next valid SRAM address (skip the next byte)
        dest += 2; 
    }

    lock_sram();
    // enable_interrupts(); 
}

/**
 * Load game data from the Sega Genesis SRAM.
 * Returns 1 if successful (valid data), 0 otherwise.
 */
uint8_t load_game_from_sram(GameSaveData* data) {
    // disable_interrupts(); // Disable interrupts

    const volatile uint8_t* src = SRAM_START_ADDR;
    uint8_t* dest = (uint8_t*)data;

    for (long unsigned int i = 0; i < sizeof(GameSaveData); i++) {
        // Read the byte from the SRAM source
        dest[i] = *src;
        // Move to the next valid SRAM address (skip the next byte)
        src += 2;
    }

    // enable_interrupts(); // Re-enable interrupts

    // Verify data integrity using the magic number and checksum
    if (data->magic != 0xABCE) { // Check if the save data has been initialized
	vdp_text_clear(VDP_PLAN_A, 0, 16, 40);
	vdp_puts(VDP_PLAN_A, "incorrect magic", 0, 16);
        return 0; 
    }
    if (data->checksum != calculate_checksum(data)) { // Check if data is corrupted
	vdp_text_clear(VDP_PLAN_A, 0, 16, 40);
	vdp_puts(VDP_PLAN_A, "checksum mismatch", 0, 16);
//        return 0;
	return 1;
    }

    return 1; // Success
}

GameSaveData mySave;

void savegame_init(void) {


    // Try to load data first
    if (load_game_from_sram(&mySave)) {
        // Data loaded successfully, continue game
        // ... use mySave.player_score, etc.
      for (int i=0; i<16; i++) {
	gateseq[i] = mySave.sequence[i];
	accseq[i] = mySave.accent[i];
	speedseq[i] = mySave.speed[i];
      }
      vdp_text_clear(VDP_PLAN_A, 0, 16, 40);
      vdp_puts(VDP_PLAN_A, "saved sequence loaded", 0, 16);
    } else {
        // No valid save data found, start a new game and initialize structure
        mySave.magic = 0xABCE; // Set magic number
	for (int i=0; i<16; i++) {
	  mySave.sequence[i] = gateseq[i];
	  mySave.sequence[i] = accseq[i];
	  mySave.speed[i] = speedseq[i];
	}
        // Calculate and set initial checksum
        mySave.checksum = calculate_checksum(&mySave); 
        // Save the initial data to SRAM immediately
        save_game_to_sram(&mySave);
    }
}

void savegame() {

  for (int i=0; i<16; i++) {
    mySave.sequence[i] = gateseq[i];
    mySave.accent[i] = accseq[i];
    mySave.speed[i] = speedseq[i];
  }
  mySave.checksum = calculate_checksum(&mySave); // Update checksum before saving
  save_game_to_sram(&mySave);
  vdp_text_clear(VDP_PLAN_A, 0, 16, 40);
  vdp_puts(VDP_PLAN_A, "sequence saved", 0, 16);
}

void play_sample() {

  Z80_requestBus(1);
  Z80_write(playCommand_addr, 1);
  Z80_releaseBus();

}

void stop_sample() {
  Z80_requestBus(1);
  Z80_write(stopCommand_addr, 1);
  Z80_releaseBus();  
}

void set_sample_length(uint16_t length) {
  Z80_requestBus(1);
  Z80_write(sampleLength_addr, length & 0x00FF);
  Z80_write(sampleLength_addr+1, length >> 8);
  Z80_releaseBus();  
}

void set_sample_start(uint16_t length) {
  Z80_requestBus(1);
  Z80_write(sampleStart_addr, length & 0x00FF);
  Z80_write(sampleStart_addr+1, length >> 8);
  Z80_releaseBus();  
}

void set_accent(int accent) {
  Z80_requestBus(1);
  if (accent) {
    Z80_write(accent_addr, 1);
  } else {
    Z80_write(accent_addr, 0);
  }

  Z80_releaseBus();  
}

void set_dacSpeed(uint8_t speed) {
  Z80_requestBus(1);
  Z80_write(speed_addr, speed);
  Z80_releaseBus();  
}

void set_amen_bank() {
  Z80_requestBus(1);
  uint32_t pcmaddr = (uint32_t)amen_unsigned_raw;
  uint16_t bank = pcmaddr >> 15;
  Z80_setBank(bank);
  Z80_releaseBus();
}


char s[255] = "";

ControllerState player1_state;
int downpressed = 0;
int uppressed = 0;
int rightpressed = 0;
int leftpressed = 0;
int apressed = 0;

int main() {

  vdp_init();
  enable_ints;
    
  vdp_color(0, 0x080);

  Z80_init();
  
  Z80_loadDriverInternal(z80driver_bin, z80driver_bin_len);

  savegame_init();

  int frame = 0;
  int laststep = 0;
  int selectstep = 0;
  int lastselectstep = 0;

  for (int step = 0; step < 16; step++) {
    sprintf(s, "%02d", step);
    vdp_puts(VDP_PLAN_A, s, 3, step);
  }

  for (int step = 0; step < 16; step++) {
    sprintf(s, "%02d", gateseq[step]);
    vdp_puts(VDP_PLAN_A, s, 6, step);
  }  

  for (int step = 0; step < 16; step++) {
    sprintf(s, "%02d", accseq[step]);
    vdp_puts(VDP_PLAN_A, s, 9, step);
  }  

  for (int step = 0; step < 16; step++) {
    sprintf(s, "%02X", speedseq[step]);
    vdp_puts(VDP_PLAN_A, s, 12, step);
  }  

  vdp_puts(VDP_PLAN_A, "-->", 0, 0);
  vdp_puts(VDP_PLAN_A, ">", 5, 0);
  vdp_puts(VDP_PLAN_A, "<", 8, 0);

  set_amen_bank(); // let z80 access our pcm data

  while(1) {

    read_controller1(&player1_state);

    if (player1_state.down) {
      if (!downpressed) {
	selectstep = (selectstep + 1) % 16;
	downpressed = 1;
      }
    } else {
      downpressed = 0;
    }

    if (player1_state.up) {
      if (!uppressed) {
	selectstep = selectstep - 1;
	if (selectstep < 0) selectstep = 15;
	uppressed = 1;
      }
    } else {
      uppressed = 0;
    }

    if (player1_state.left) {
      if (!leftpressed) {
	if (column == 0) {
	  gateseq[selectstep]--;
	  if (gateseq[selectstep] < 0) gateseq[selectstep] = 0;
	
	  savegame();

	  vdp_text_clear(VDP_PLAN_A, 6, selectstep, 2);
	  sprintf(s, "%02d", gateseq[selectstep]);
	  vdp_puts(VDP_PLAN_A, s, 6, selectstep);      
	} else if (column == 1) {
	  accseq[selectstep]--;
	  if (accseq[selectstep] < 0) accseq[selectstep] = 0;
	
	  savegame();

	  vdp_text_clear(VDP_PLAN_A, 9, selectstep, 2);
	  sprintf(s, "%02d", accseq[selectstep]);
	  vdp_puts(VDP_PLAN_A, s, 9, selectstep);      

	} else if (column == 2) {

	  speedseq[selectstep]--;
	  if (speedseq[selectstep] < 1) speedseq[selectstep] = 1;
	
	  savegame();

	  vdp_text_clear(VDP_PLAN_A, 12, selectstep, 2);
	  sprintf(s, "%02d", speedseq[selectstep]);
	  vdp_puts(VDP_PLAN_A, s, 12, selectstep);      
	}
	leftpressed = 1;	  
      } else {
	leftpressed = 0;
      }
    }

    if (player1_state.right) {
      if (!rightpressed) {
	if (column == 0) {

	  gateseq[selectstep]++;
	  if (gateseq[selectstep] > sampleMax) gateseq[selectstep] = sampleMax;

	  savegame();

	  vdp_text_clear(VDP_PLAN_A, 6, selectstep, 2);
	  sprintf(s, "%02d", gateseq[selectstep]);
	  vdp_puts(VDP_PLAN_A, s, 6, selectstep);
	  rightpressed = 1;
	} else if (column == 1) {
	  accseq[selectstep]++;
	  if (accseq[selectstep] > 1) accseq[selectstep] = 1;

	  savegame();

	  vdp_text_clear(VDP_PLAN_A, 9, selectstep, 2);
	  sprintf(s, "%02d", accseq[selectstep]);
	  vdp_puts(VDP_PLAN_A, s, 9, selectstep);

	} else if (column == 2) {

	  speedseq[selectstep]++;
	  if (speedseq[selectstep] > 255) speedseq[selectstep] = 255;
	
	  savegame();

	  vdp_text_clear(VDP_PLAN_A, 12, selectstep, 2);
	  sprintf(s, "%02d", speedseq[selectstep]);
	  vdp_puts(VDP_PLAN_A, s, 12, selectstep);      
	  
	}
	rightpressed = 1;
      }	
    } else {
      rightpressed = 0;
    }


    if (player1_state.a) {
      if (!apressed) {
	column = (column + 1) % COLUMN_COUNT;
	if (column == 1) {
	  vdp_text_clear(VDP_PLAN_A, 5, selectstep, 1);
	  vdp_text_clear(VDP_PLAN_A, 8, selectstep, 1);      
	  vdp_puts(VDP_PLAN_A, ">", 8, selectstep);
	  vdp_puts(VDP_PLAN_A, "<", 11, selectstep);            
	} else if (column == 0) {
	  vdp_text_clear(VDP_PLAN_A, 11, selectstep, 1);
	  vdp_text_clear(VDP_PLAN_A, 14, selectstep, 1);      
	  vdp_puts(VDP_PLAN_A, ">", 5, selectstep);
	  vdp_puts(VDP_PLAN_A, "<", 8, selectstep);            
	} else if (column == 2) {
	  vdp_text_clear(VDP_PLAN_A, 8, selectstep, 1);
	  vdp_text_clear(VDP_PLAN_A, 11, selectstep, 1);      
	  vdp_puts(VDP_PLAN_A, ">", 11, selectstep);
	  vdp_puts(VDP_PLAN_A, "<", 14, selectstep);
	}
	oldcolumn = column;
	apressed = 1;
      }
    } else {
      apressed = 0;
    }

    if (seqpos != laststep) {
      vdp_text_clear(VDP_PLAN_A, 0, laststep, 3);
      vdp_puts(VDP_PLAN_A, "-->", 0, seqpos);      
      laststep = seqpos;
    }
    if (selectstep != lastselectstep) {
      if (column == 0) {
	vdp_text_clear(VDP_PLAN_A, 5, lastselectstep, 1);
	vdp_text_clear(VDP_PLAN_A, 8, lastselectstep, 1);      
	vdp_puts(VDP_PLAN_A, ">", 5, selectstep);
	vdp_puts(VDP_PLAN_A, "<", 8, selectstep);            
      } else if (column == 1) {
	vdp_text_clear(VDP_PLAN_A, 8, lastselectstep, 1);
	vdp_text_clear(VDP_PLAN_A, 11, lastselectstep, 1);      
	vdp_puts(VDP_PLAN_A, ">", 8, selectstep);
	vdp_puts(VDP_PLAN_A, "<", 11, selectstep);            
      } else if (column == 2) {
	vdp_text_clear(VDP_PLAN_A, 11, lastselectstep, 1);
	vdp_text_clear(VDP_PLAN_A, 14, lastselectstep, 1);      
	vdp_puts(VDP_PLAN_A, ">", 11, selectstep);
	vdp_puts(VDP_PLAN_A, "<", 14, selectstep);            
      }
      lastselectstep = selectstep;
    }

    if (frame % framemod == 0) {
      if (gateseq[seqpos]) {
	stop_sample();
	if (accseq[seqpos] == 1) {
	  set_accent(1);
	} else {
	  set_accent(0);
	}
	if (gateseq[seqpos] == 1) {
	  set_sample_start(0);
	  set_sample_length(amen_unsigned_raw_len);
	} else if (gateseq[seqpos] == 2) {
	  set_sample_start(amen_unsigned_raw_len / 2 - 1);
	  set_sample_length(amen_unsigned_raw_len / 2);
	}
	set_dacSpeed(speedseq[seqpos]);
	play_sample();
      }
      seqpos = (seqpos + 1) % 16;
    }

    vdp_vsync();
    frame++;
  }
	
  return 0;
}
