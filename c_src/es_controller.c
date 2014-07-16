/*
 * Copyright (c) 2009 Xilinx, Inc.  All rights reserved.
 *
 * Xilinx, Inc.
 * XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS" AS A
 * COURTESY TO YOU.  BY PROVIDING THIS DESIGN, CODE, OR INFORMATION AS
 * ONE POSSIBLE   IMPLEMENTATION OF THIS FEATURE, APPLICATION OR
 * STANDARD, XILINX IS MAKING NO REPRESENTATION THAT THIS IMPLEMENTATION
 * IS FREE FROM ANY CLAIMS OF INFRINGEMENT, AND YOU ARE RESPONSIBLE
 * FOR OBTAINING ANY RIGHTS YOU MAY REQUIRE FOR YOUR IMPLEMENTATION.
 * XILINX EXPRESSLY DISCLAIMS ANY WARRANTY WHATSOEVER WITH RESPECT TO
 * THE ADEQUACY OF THE IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO
 * ANY WARRANTIES OR REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE
 * FROM CLAIMS OF INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 */


#include "xparameters.h"
#include "es_controller.h"
#include "es_simple_eye_acq.h"
#include "drp.h"
#include "xaxi_eyescan.h"
#include <semaphore.h>
#include "safe_printf.h"

sem_t eyescan_sem[4];

void eyescan_lock(int lane) {
	if( sem_wait( &eyescan_sem[lane%4] ) == -1 ) {
		sem_init( &eyescan_sem[lane%4] , 0 , 1 );
		sem_wait( &eyescan_sem[lane%4] );
	}
}

void eyescan_unlock(int lane) {
	sem_post( &eyescan_sem[lane%4] );
}

#define MAX_NUMBER_OF_LANES 48

eye_scan * eye_scan_lanes[MAX_NUMBER_OF_LANES];

eye_scan * get_eye_scan_lane( int lane ) {
	if( lane > MAX_NUMBER_OF_LANES )
		return NULL;
    return eye_scan_lanes[lane];
}

void init_eye_scan_struct( eye_scan * p_lane ) {
	int idx=0;
	//Initialize struct members to default values
	p_lane->enable = FALSE;
	p_lane->state = WAIT_STATE;
	p_lane->p_upload_rdy = 0;
	p_lane->pixels = malloc( sizeof(eye_scan_pixel) * NUM_PIXELS_TOTAL );

	p_lane->horz_offset = 0;
	p_lane->vert_offset = 0;
	p_lane->ut_sign = 0;
	p_lane->pixel_count = 0;

	p_lane->lane_number = 0;

	p_lane->prescale = 0;
}

/*
 * Function: init_eye_scan
 * Description: Initialize eye_scan data struct members and write related attributes through DRP
 *
 * Parameters:
 * p_lane: pointer to eye scan data structure
 *
 * Returns: True if completed, False if not
 */


int init_eye_scan(eye_scan* p_lane, u8 curr_lane) {
	u8 i;

	//Scan parameters should already be loaded into the structs by host pc

	if(p_lane->enable == 0){
	  p_lane->state = WAIT_STATE;
	  return FALSE;
	}

	//Initialize other struct members to default values
	p_lane->state = RESET_STATE;

	p_lane->lane_number = curr_lane;
	//p_lane->lane_name[0] = lane_name;

	/* And then decide whether or not to enable eyescan circuitry port. */

	if( 1 ) {
    xaxi_eyescan_write_channel_drp(curr_lane,DRP_ADDR_ES_CONTROL,0x0300);
    u16 esval = 0x2070; // Enable the eyescan circuity.
    drp_write(esval , PMA_RSV2 , curr_lane );
    u16 valrsv2 = drp_read( PMA_RSV2 , curr_lane );
    //xil_printf("PMA_RSV2 = 0x%04x\n",valrsv2);
    if( esval != valrsv2 )
    	xil_printf("ERROR: Failed to write PMA_RSV2 with expected value\n");
	}

	xaxi_eyescan_write_channel_reg(curr_lane, XAXI_EYESCAN_TXCFG, 1);
	xaxi_eyescan_write_channel_reg(curr_lane, XAXI_EYESCAN_RXCFG, 1);
	//xaxi_eyescan_write_channel_reg(curr_lane, XAXI_EYESCAN_RESET, 0xFFFF);
	//xaxi_eyescan_write_channel_reg(curr_lane, XAXI_EYESCAN_RESET, 0);
	//xaxi_eyescan_write_channel_reg(curr_lane, XAXI_EYESCAN_RESET, 0x8000);
	//xaxi_eyescan_write_channel_reg(curr_lane, XAXI_EYESCAN_RESET, 0);

	xaxi_eyescan_write_channel_reg(curr_lane, XAXI_EYESCAN_RESET, 0x0020);
	xaxi_eyescan_write_channel_reg(curr_lane, XAXI_EYESCAN_RESET, 0);
	xaxi_eyescan_write_channel_reg(curr_lane, XAXI_EYESCAN_RESET, 0x0010);
	xaxi_eyescan_write_channel_reg(curr_lane, XAXI_EYESCAN_RESET, 0);

	u32 reset_val = xaxi_eyescan_read_channel_reg(curr_lane,XAXI_EYESCAN_RESET);
	int retries = 0;
	while( reset_val != 0x0000000F && retries < 2 ) {
		xaxi_eyescan_write_channel_reg(curr_lane, XAXI_EYESCAN_RESET, 0x0020);
		xaxi_eyescan_write_channel_reg(curr_lane, XAXI_EYESCAN_RESET, 0);
		xaxi_eyescan_write_channel_reg(curr_lane, XAXI_EYESCAN_RESET, 0x0010);
		xaxi_eyescan_write_channel_reg(curr_lane, XAXI_EYESCAN_RESET, 0);
		sleep(1000);
		reset_val = xaxi_eyescan_read_channel_reg(curr_lane,XAXI_EYESCAN_RESET);
		xil_printf("Channel %d: Reset register(init): %08x\n",curr_lane,reset_val);
		retries++;
	}

	u32 monreg = xaxi_eyescan_read_channel_reg(curr_lane,XAXI_EYESCAN_MONITOR);
	//xil_printf("Channel %d: Monitor register: %08lx\n",curr_lane,monreg);
	u32 errcnt = monreg & 0x80FF;
	//xil_printf("Channel %d: Error Count: %08lx\n",curr_lane,errcnt);

	//Write ES_ERRDET_EN, ES_EYESCAN_EN attributes to enable eye scan
	drp_write(0x1, ES_EYESCAN_EN, curr_lane );
	drp_write(0x1, ES_ERRDET_EN, curr_lane );

	//Write ES_SDATA_MASK0-1 attribute based on parallel data width
	switch (p_lane->data_width)
		{
		  case 40: {
			  drp_write(0x0000, ES_SDATA_MASK0, curr_lane);
			  drp_write(0x0000, ES_SDATA_MASK1, curr_lane);
			  }
			  break;
		  case 32: {
			  drp_write(0x00FF, ES_SDATA_MASK0, curr_lane);
			  drp_write(0x0000, ES_SDATA_MASK1, curr_lane);
			  }
			  break;
		  case 20: {
			  drp_write(0xFFFF, ES_SDATA_MASK0, curr_lane);
			  drp_write(0x000F, ES_SDATA_MASK1, curr_lane);
			  }
			  break;
		  case 16: {
			  drp_write(0xFFFF, ES_SDATA_MASK0, curr_lane);
			  drp_write(0x00FF, ES_SDATA_MASK1, curr_lane);
			  }
			  break;
		  default:{
			  drp_write(0xFFFF, ES_SDATA_MASK0, curr_lane);
			  drp_write(0xFFFF, ES_SDATA_MASK1, curr_lane);
			  }
		}

	//Write SDATA_MASK2-4 attributes.  Values are same for all data widths (16, 20,32,40)
	drp_write(0xFF00, ES_SDATA_MASK2, curr_lane);
	for(i=ES_SDATA_MASK3; i <= ES_SDATA_MASK4; i++){
			drp_write(0xFFFF, i, curr_lane);
	}

	//Write ES_PRESCALE attribute to 0
	drp_write(0x0000, ES_PRESCALE, curr_lane);

	//Write ES_QUAL_MASK attribute to all 1's
	for(i = ES_QUAL_MASK0; i <= ES_QUAL_MASK4; i++){
		drp_write(0xFFFF, i, curr_lane);
	}

	//monreg = xaxi_eyescan_read_channel_reg(curr_lane,XAXI_EYESCAN_MONITOR);
	//xil_printf("Channel %d: Monitor register: %08lx\n",curr_lane,monreg);
	//errcnt = monreg & 0x80FF;
	//xil_printf("Channel %d: Error Count: %08lx\n",curr_lane,errcnt);

	//u32 read1 = xaxi_eyescan_read_channel_reg(curr_lane,XAXI_EYESCAN_RESET);
	//xil_printf("Channel %d: Reset register(init): %08x\n",curr_lane,read1);

	return TRUE;
}

void *es_controller_thread(char * arg)
{
	//sleep(30000);
	xil_printf( "staring es_controller_thread\n");
	// Global initialization:
	xaxi_eyescan_write_global(XAXI_EYESCAN_GLOBAL_RESET,0x2);
	u32 n_gtx = xaxi_eyescan_read_global(XAXI_EYESCAN_NGTX);
	//u32 n_quad = n_gtx >> 16;
	n_gtx &= 0x00FF;
	xil_printf( "n_gtx %d\n" , n_gtx );
    //Eye scan data structure
	u32 num_lanes = n_gtx;
    u8 curr_lane;

    u8 test_enable[MAX_NUMBER_OF_LANES];
    for( curr_lane=0; curr_lane<num_lanes; curr_lane++ ) {
    	eye_scan_lanes[curr_lane] = malloc( sizeof(eye_scan) );
    	test_enable[curr_lane] = FALSE;
    	init_eye_scan_struct( eye_scan_lanes[curr_lane] );
    }
    //Initialization after startup or reset
    for(curr_lane=0;curr_lane < num_lanes;curr_lane++){
        eye_scan_lanes[curr_lane]->state = WAIT_STATE;//Initialize each lane's state to WAIT_STATE
    }
    //Main Loop
    while (1)
    {
    	for(curr_lane=0; curr_lane<num_lanes ;curr_lane++){
    		//Start a new scan if it's not currently running
    		sleep(100);
    		if(test_enable[curr_lane] == FALSE) {
    			//eyescan_lock(curr_lane);
    			test_enable[curr_lane] = init_eye_scan(eye_scan_lanes[curr_lane], curr_lane);//Initialize scan parameters
    			//eyescan_unlock(curr_lane);
    		}

    		if( test_enable[curr_lane] == FALSE )
    			continue;

    		//xil_printf( "lane %d is enabled\n" , curr_lane );

    		//eyescan_lock(curr_lane);
    		es_simple_eye_acq(eye_scan_lanes[curr_lane]);
    		//eyescan_unlock(curr_lane);

    		if( eye_scan_lanes[curr_lane]->state == DONE_STATE )
    			eye_scan_lanes[curr_lane]->p_upload_rdy = 1;
    	}
    }
    free(eye_scan_lanes);
}

void eyescan_global_debug( char * dbgstr ) {
	safe_sprintf( dbgstr , "Global registers:\r\n" );

	safe_sprintf( dbgstr , "%sn_gtx        0x%08lx\r\n" , dbgstr , xaxi_eyescan_read_global(XAXI_EYESCAN_NGTX) );
	safe_sprintf( dbgstr , "%sn_left       0x%08lx\r\n" , dbgstr , xaxi_eyescan_read_global(XAXI_EYESCAN_NLEFT) );
	safe_sprintf( dbgstr , "%sn_right      0x%08lx\r\n" , dbgstr , xaxi_eyescan_read_global(XAXI_EYESCAN_NRIGHT) );
	safe_sprintf( dbgstr , "%sqpll_lock    0x%08lx\r\n" , dbgstr , xaxi_eyescan_read_global(XAXI_EYESCAN_QPLL_LOCK) );
	safe_sprintf( dbgstr , "%sqpll_lost    0x%08lx\r\n" , dbgstr , xaxi_eyescan_read_global(XAXI_EYESCAN_QPLL_LOST) );
	safe_sprintf( dbgstr , "%sglobal_reset 0x%08lx\r\n" , dbgstr , xaxi_eyescan_read_global(XAXI_EYESCAN_GLOBAL_RESET) );

	return;
}

void eyescan_debugging( int lane , char * dbgstr ) {
	eyescan_global_debug( dbgstr );
	if( lane < 0 )
		return;
	safe_sprintf( dbgstr , "%seyescan_reset   0x%08lx\r\n" , dbgstr , xaxi_eyescan_read_channel_reg( lane , XAXI_EYESCAN_RESET ) );
	safe_sprintf( dbgstr , "%seyescan_txcfg   0x%08lx\r\n" , dbgstr , xaxi_eyescan_read_channel_reg( lane , XAXI_EYESCAN_TXCFG ) );
	safe_sprintf( dbgstr , "%seyescan_rxcfg   0x%08lx\r\n" , dbgstr , xaxi_eyescan_read_channel_reg( lane , XAXI_EYESCAN_RXCFG ) );
	safe_sprintf( dbgstr , "%seyescan_clkcfg  0x%08lx\r\n" , dbgstr , xaxi_eyescan_read_channel_reg( lane , XAXI_EYESCAN_CLKCFG ) );
	safe_sprintf( dbgstr , "%seyescan_monitor 0x%08lx\r\n" , dbgstr , xaxi_eyescan_read_channel_reg( lane , XAXI_EYESCAN_MONITOR ) );
	safe_sprintf( dbgstr , "%seyescan_cursor  0x%08lx\r\n" , dbgstr , xaxi_eyescan_read_channel_reg( lane , XAXI_EYESCAN_CURSOR ) );

	safe_sprintf( dbgstr , "%ses_control        0x%04x\r\n" , dbgstr , drp_read( ES_CONTROL , lane ) );
	safe_sprintf( dbgstr , "%ses_horz_offset    0x%04x\r\n" , dbgstr , drp_read( ES_HORZ_OFFSET , lane ) );
	safe_sprintf( dbgstr , "%ses_prescale       0x%04x\r\n" , dbgstr , drp_read( ES_PRESCALE , lane ) );
	safe_sprintf( dbgstr , "%ses_vert_offset    0x%04x\r\n" , dbgstr , drp_read( ES_VERT_OFFSET , lane ) );
	safe_sprintf( dbgstr , "%ses_control_status 0x%04x\r\n" , dbgstr , drp_read( ES_CONTROL_STATUS , lane ) );
	safe_sprintf( dbgstr , "%ses_error_count    0x%04x\r\n" , dbgstr , drp_read( ES_ERROR_COUNT , lane ) );
	safe_sprintf( dbgstr , "%ses_sample_count   0x%04x\r\n" , dbgstr , drp_read( ES_SAMPLE_COUNT , lane ) );
	safe_sprintf( dbgstr , "%ses_eyescan_en     0x%04x\r\n" , dbgstr , drp_read( ES_EYESCAN_EN , lane ) );
	safe_sprintf( dbgstr , "%ses_errdet_en      0x%04x\r\n" , dbgstr , drp_read( ES_ERRDET_EN , lane ) );
	safe_sprintf( dbgstr , "%ses_sdata_mask0    0x%04x\r\n" , dbgstr , drp_read( ES_SDATA_MASK0 , lane ) );
	safe_sprintf( dbgstr , "%ses_sdata_mask1    0x%04x\r\n" , dbgstr , drp_read( ES_SDATA_MASK1 , lane ) );
	safe_sprintf( dbgstr , "%ses_sdata_mask2    0x%04x\r\n" , dbgstr , drp_read( ES_SDATA_MASK2 , lane ) );
	safe_sprintf( dbgstr , "%ses_sdata_mask3    0x%04x\r\n" , dbgstr , drp_read( ES_SDATA_MASK3 , lane ) );
	safe_sprintf( dbgstr , "%ses_sdata_mask4    0x%04x\r\n" , dbgstr , drp_read( ES_SDATA_MASK4 , lane ) );
	safe_sprintf( dbgstr , "%ses_qual_mask0     0x%04x\r\n" , dbgstr , drp_read( ES_QUAL_MASK0 , lane ) );
	safe_sprintf( dbgstr , "%ses_qual_mask1     0x%04x\r\n" , dbgstr , drp_read( ES_QUAL_MASK1 , lane ) );
	safe_sprintf( dbgstr , "%ses_qual_mask2     0x%04x\r\n" , dbgstr , drp_read( ES_QUAL_MASK2 , lane ) );
	safe_sprintf( dbgstr , "%ses_qual_mask3     0x%04x\r\n" , dbgstr , drp_read( ES_QUAL_MASK3 , lane ) );
	safe_sprintf( dbgstr , "%ses_qual_mask4     0x%04x\r\n" , dbgstr , drp_read( ES_QUAL_MASK4 , lane ) );
	safe_sprintf( dbgstr , "%spma_rsv2          0x%04x\r\n" , dbgstr , drp_read( PMA_RSV2 , lane ) );

	return;
}

void eyescan_debug_addr( int lane , u32 drp_addr , char * dbgstr ) {
	safe_sprintf( dbgstr , "lane %d addr 0x%04x val 0x%04x\r\n" , lane , drp_addr , xaxi_eyescan_read_channel_drp( lane , drp_addr ) );
	return;
}
