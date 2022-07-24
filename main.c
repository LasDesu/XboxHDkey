#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "sgio.h"
#include "hdparm.h"

#include "BootHddKey.h"

int verbose = 0;
int prefer_ata12 = 0;

const int timeout_15secs = 15;

static __u16 *id;

static void get_identify_data ( int fd )
{
	static __u8 args[4+512];
	int i;

	if (id)
		return;
	memset(args, 0, sizeof(args));
	args[0] = ATA_OP_IDENTIFY;
	args[3] = 1;	/* sector count */
	if (do_drive_cmd(fd, args, 0)) {
		prefer_ata12 = 0;
		memset(args, 0, sizeof(args));
		args[0] = ATA_OP_PIDENTIFY;
		args[3] = 1;	/* sector count */
		if (do_drive_cmd(fd, args, 0)) {
			if (verbose)
				perror(" HDIO_DRIVE_CMD(identify) failed");
			return;
		}
	}
	/* byte-swap the little-endian IDENTIFY data to match byte-order on host CPU */
	id = (void *)(args + 4);
	for (i = 0; i < 0x100; ++i) {
		unsigned char *b = (unsigned char *)&id[i];
		id[i] = b[0] | (b[1] << 8);	/* le16_to_cpu() */
	}
}

static int security_freeze   = 0;
static int security_master = 0, security_mode = 0;
static unsigned int security_command = ATA_OP_SECURITY_UNLOCK;
static char security_password[33];

static void
do_set_security (int fd)
{
	int err = 0;
	const char *description;
	struct hdio_taskfile *r;
	__u8 *data;

	r = malloc(sizeof(struct hdio_taskfile) + 512);
	if (!r) {
		err = errno;
		perror("malloc()");
		exit(err);
	}

	memset(r, 0, sizeof(struct hdio_taskfile) + 512);
	r->cmd_req	= TASKFILE_CMD_REQ_OUT;
	r->dphase	= TASKFILE_DPHASE_PIO_OUT;
	r->obytes	= 512;
	r->lob.command	= security_command;
	r->oflags.bits.lob.nsect = 1;
	r->lob.nsect    = 1;
	data		= (__u8*)r->data;
	data[0]		= security_master & 0x01;
	memcpy(data+2, security_password, 32);

	r->oflags.bits.lob.command = 1;
	r->oflags.bits.lob.feat    = 1;

	switch (security_command) {
		case ATA_OP_SECURITY_DISABLE:
			description = "SECURITY_DISABLE";
			break;
		case ATA_OP_SECURITY_UNLOCK:
			description = "SECURITY_UNLOCK";
			break;
		case ATA_OP_SECURITY_SET_PASS:
			description = "SECURITY_SET_PASS";
			data[1] = (security_mode & 0x01);
			if (security_master) {
				/* increment master-password revision-code */
				__u16 revcode;
				get_identify_data(fd);
				if (!id)
					exit(EIO);
				revcode = id[92];
				if (revcode == 0xfffe)
					revcode = 0;
				revcode += 1;
				data[34] = revcode;
				data[35] = revcode >> 8;
			}
			break;
		default:
			fprintf(stderr, "BUG in do_set_security(), command1=0x%x\n", security_command);
			exit(EINVAL);
	}

	printf(" Issuing %s command", description);
	if (security_command == ATA_OP_SECURITY_SET_PASS)
		printf(", mode=%s", data[1] ? "max" : "high");
	printf("\n");

	/*
	 * The Linux kernel IDE driver (until at least 2.6.12) segfaults on the first
	 * command when issued on a locked drive, and the actual erase is never issued.
	 * One could patch the code to issue separate commands for erase prepare and
	 * erase to erase a locked drive.
	 *
	 * We would like to issue these commands consecutively, but since the Linux
	 * kernel until at least 2.6.12 segfaults on each command issued the second will
	 * never be executed.
	 *
	 * One is at least able to issue the commands consecutively in two hdparm invocations,
	 * assuming the segfault isn't followed by an oops.
	 */
	if (security_command == ATA_OP_SECURITY_DISABLE) {
		/* First attempt an unlock  */
		r->lob.command = ATA_OP_SECURITY_UNLOCK;
		if ((do_taskfile_cmd(fd, r, timeout_15secs))) {
			err = errno;
			perror("SECURITY_UNLOCK");
		} else {
			/* Then the security disable */
			r->lob.command = security_command;
			if ((do_taskfile_cmd(fd, r, timeout_15secs))) {
				err = errno;
				perror("SECURITY_DISABLE");
			}
		}
	} else if (security_command == ATA_OP_SECURITY_UNLOCK) {
		if ((do_taskfile_cmd(fd, r, timeout_15secs))) {
			err = errno;
			perror("SECURITY_UNLOCK");
		}
	} else if (security_command == ATA_OP_SECURITY_SET_PASS) {
		if ((do_taskfile_cmd(fd, r, timeout_15secs))) {
			err = errno;
			perror("SECURITY_SET_PASS");
		}
	} else {
		fprintf(stderr, "BUG in do_set_security(), command2=0x%x\n", security_command);
		err = EINVAL;
	}
	free(r);
	if (err)
		exit(err);
}

static void print_array( const uint8_t *data, unsigned len )
{
	while ( len -- )
	{
		printf("%02X", *data );
		data ++;
	}
}
#define PRINT_ARR(a) print_array( a, sizeof(a) )

static int parse_eeprom(  const char *filename, void *hdd_key )
{
	FILE *fp;
	static EEPROMDATA eeprom;

	fp = fopen( filename, "rb" );
	if ( !fp )
	{
		fprintf( stderr, "Failed to open EEPROM file\n" );
		return 1;
	}
	fread( &eeprom, 1, 0x100, fp );
	fclose( fp );

	BootEepromPrintInfo( &eeprom );
	BootDecryptEEPROM( &eeprom );
	memcpy( hdd_key, eeprom.HDDKkey, HDDKEY_SIZE );

	return 0;
}

static void process_cmd( int fd, const __u16 *id, void *hdd_key )
{
	unsigned char HDDPass[256] = {0};
	unsigned s_length = 20;
	unsigned m_length = 40;
	unsigned char pw[32];
	unsigned char model[40 + 1], serial[20 + 1];

	s_length = copy_swap_trim( serial, (const char *)&id[10], s_length );
	m_length = copy_swap_trim( model, (const char *)&id[27], m_length );
	printf( "HDD model: %s\n HDD serial: %s\n", model, serial );

	HMAC_SHA1( HDDPass, hdd_key, 0x10, model, m_length, serial, s_length );
	/* only 20 bytes used */
	memcpy( security_password, HDDPass, 20 );
	memset( security_password + 20, 0, 12 );

	printf( "HDD password: " );
	print_array( HDDPass, 32 );
	printf( "\n" );

	do_set_security( fd );
}

static void usage()
{
	printf( "Usage:\n" );
	printf( "  xboxhdkey [options] <device>\n\n" );
	printf( "Options:\n" );
	printf( "  -e <file>    - compute HDD password from EEPROM file\n" );
	printf( "  -k <key>     - compute HDD password from HDD key (hex)\n" );
	printf( "  -l           - lock HDD\n" );
	printf( "  -u           - unlock HDD (default)\n" );
	printf( "  -d           - disable HDD security\n" );
	printf( "  -h           - this screen\n" );
	printf( "\n" );
}

int main( int argc, char **argv )
{
	const char *device = NULL;
	const char *eeprom_file = NULL;
	const char *hdd_key_str = NULL;
	char hdd_key[HDDKEY_SIZE];
	int fd;
	int err;
	int c;
	
	printf( "xboxhdkey - lock/unlock Original Xbox HDD\n\n" );
	while ( (c = getopt(argc, argv, "he:k:lud")) != -1 )
	{
		switch ( c )
		{
			case 'h':
				usage();
				exit( 1 );
				break;
			case 'e':
				eeprom_file = optarg;
				break;
			case 'k':
				hdd_key_str = optarg;
				break;
			case 'l':
				security_command = ATA_OP_SECURITY_SET_PASS;
				break;
			case 'u':
				security_command = ATA_OP_SECURITY_UNLOCK;
				break;
			case 'd':
				security_command = ATA_OP_SECURITY_DISABLE;
				break;
			default:
				usage();
				exit( 1 );
		}
	}

	if ( optind < argc )
		device = argv[optind ++];

	if ( !device )
	{
		fprintf( stderr, "Drive not specified\n" );
		exit( 1 );
	}

	if ( !eeprom_file && !hdd_key_str )
	{
		fprintf( stderr, "No EEPROM or HDD key specified\n" );
		exit( 1 );
	}

	if ( eeprom_file )
	{
		if ( parse_eeprom(  eeprom_file, &hdd_key ) )
			return 1;
	}
	else
	{
		/* parse HDD key */
		char byte[3];
		int i;

		if ( strlen( hdd_key_str ) != HDDKEY_SIZE*2 )
		{
			fprintf( stderr, "Invalid HDD key (wrong size)\n" );
			return 1;
		}

		byte[2] = '\0';
		for ( i = 0; i < HDDKEY_SIZE; i ++ )
		{
			unsigned long v;

			byte[0] = hdd_key_str[i*2 + 0];
			byte[1] = hdd_key_str[i*2 + 1];

			/* not the best solution, a bit overkill and no checking */
			v = strtoul( byte, NULL, 16 );
			hdd_key[i] = v;
		}
	}

	printf("Using HDD key: ");
	print_array( hdd_key, HDDKEY_SIZE );
	printf("\n");
	
	/* open device */
	fd = open( device, O_RDONLY | O_NONBLOCK );
	if ( fd < 0 )
	{
		int err = errno;
		perror( device );
		exit( err );
	}

	get_identify_data( fd );
	process_cmd( fd, id, hdd_key );

	close( fd );

	return 0;
}
