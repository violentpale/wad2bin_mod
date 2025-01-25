/*
 * main.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of wad2bin (https://github.com/DarkMatterCore/wad2bin).
 *
 * wad2bin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * wad2bin is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "utils.h"
#include "keys.h"
#include "bin.h"

#define PATH_COUNT       4
#define NULL_KEY_ARG    "--nullkey"
#define CONVERT_ARG   	"--convert"

int main(int argc, char **argv)
{
    int ret = 0;
    
    /* Reserve memory for an extra temporary path. */
    os_char_t *paths[PATH_COUNT + 1] = {0};
    
    CertificateChain *cert_chain = NULL;
    
    Ticket *ticket = NULL;
    
    TitleMetadata *tmd = NULL;
    TmdCommonBlock *tmd_common_block = NULL;
    
    u64 title_id = 0, parent_tid = 0;
    u32 required_ios = 0, tid_upper = 0;
    bool use_null_key = false;
    
    printf("\n" PROJECT_NAME " v" PROJECT_VERSION " (c) " PROJECT_AUTHOR ".\n");
    printf("Built on: %s %s.\n\n", __TIME__, __DATE__);
    
    if (argc < (PATH_COUNT + 1) || argc > (PATH_COUNT + 3) || strlen(argv[1]) >= MAX_PATH || strlen(argv[2]) >= MAX_PATH || strlen(argv[3]) >= MAX_PATH || \
        (strlen(argv[4]) + SD_CONTENT_PATH_MAX_LENGTH) >= MAX_PATH || (argc >= (PATH_COUNT + 2) && strlen(argv[5]) != 16) || (argc == (PATH_COUNT + 3) && (strlen(argv[6]) != strlen(NULL_KEY_ARG) || \
        strcmp(argv[6], NULL_KEY_ARG) != 0)) || \
		(argc == (PATH_COUNT + 4) && (strlen(argv[7]) != strlen(CONVERT_ARG) || strcmp(argv[7], CONVERT_ARG) != 0)))
    {
        printf("Usage: %s <keys.txt> <device.cert> <input WAD> <output dir> [<parent title ID> [" NULL_KEY_ARG "]]\n\n", argv[0]);
        printf("Paths must not exceed %u characters. Relative paths are supported.\n", MAX_PATH - 1);
        printf("The required directory tree for the *.bin file(s) will be created at the output directory.\n");
        printf("You can set your SD card root directory as the output directory.\n\n");
        printf("Notes about DLC support:\n");
        printf("* Parent title ID is only required if the input WAD is a DLC. A 16 character long hex string is expected.\n");
        printf("* If \"" NULL_KEY_ARG "\" is set after the parent title ID, a null key will be used to encrypt DLC content data.\n");
        printf("  Some older games (like Rock Band 2) depend on this to properly load DLC data when launched via the Disc Channel.\n\n");
        printf("For more information, please visit: https://github.com/" PROJECT_AUTHOR "/" PROJECT_NAME ".\n\n");
        ret = -1;
        goto out;
    }
    
    /* Allocate memory for the certificate chain, ticket and TMD. */
    cert_chain = (CertificateChain*)calloc(1, sizeof(CertificateChain));
    ticket = (Ticket*)calloc(1, sizeof(Ticket));
    tmd = (TitleMetadata*)calloc(1, sizeof(TitleMetadata));
    if (!cert_chain || !ticket || !tmd)
    {
        ERROR_MSG("Error allocating memory for certificate chain / ticket / TMD structs!");
        ret = -2;
        goto out;
    }
    
    /* Generate path buffers. */
    for(u32 i = 0; i <= PATH_COUNT; i++)
    {
        /* Allocate memory for the current path. */
        paths[i] = (os_char_t*)calloc(MAX_PATH, sizeof(os_char_t));
        if (!paths[i])
        {
            ERROR_MSG("Error allocating memory for path #%u!", i);
            ret = -3;
            goto out;
        }
        
        if (i == PATH_COUNT)
        {
            /* Save temporary path and create it. */
            os_snprintf(paths[i], MAX_PATH, "." OS_PATH_SEPARATOR PROJECT_NAME "_wad_data");
            os_mkdir(paths[i], 0777);
        } else {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
            /* Convert current path string to UTF-16. */
            /* We'll only need to perform manual conversion at this point. */
            if (!utilsConvertUTF8ToUTF16(paths[i], argv[i + 1]))
            {
                ERROR_MSG("Failed to convert path from UTF-8 to UTF-16!");
                ret = -4;
                goto out;
            }
#else
            /* Copy path. */
            os_snprintf(paths[i], MAX_PATH, "%s", argv[i + 1]);
#endif
            
            /* Check if the output directory string ends with a path separator. */
            /* If so, remove it. */
            u64 path_len = strlen(argv[i + 1]);
            if (i == (PATH_COUNT - 1) && argv[i + 1][path_len - 1] == *((u8*)OS_PATH_SEPARATOR)) paths[i][path_len - 1] = (os_char_t)0;
        }
    }
    
    /* Check if the user provided a parent title ID. */
    if (argc >= (PATH_COUNT + 2))
    {
        /* Parse parent title ID. */
        if (!keysParseHexKey((u8*)&parent_tid, NULL, argv[5], 8, false))
        {
            ERROR_MSG("Failed to parse parent title ID!\n");
            ret = -5;
            goto out;
        }
        
        /* Byteswap parent title ID. */
        parent_tid = bswap_64(parent_tid);
        
        /* Check if the TID upper u32 is valid. */
        u32 parent_tid_upper = TITLE_UPPER(parent_tid);
        if (parent_tid_upper != TITLE_TYPE_DISC_GAME && parent_tid_upper != TITLE_TYPE_DOWNLOADABLE_CHANNEL && parent_tid_upper != TITLE_TYPE_DISC_BASED_CHANNEL)
        {
            ERROR_MSG("Invalid parent title ID category! (%08" PRIx32 ").\nOnly disc-based game IDs, downloadable channel IDs and disc-based channel IDs are supported.\n", parent_tid_upper);
            ret = -6;
            goto out;
        }
        
        /* Enable null key usage if needed. */
        use_null_key = (argc == (PATH_COUNT + 3));
    }
    
    /* Load keydata and device certificate. */
    if (!keysLoadKeyDataAndDeviceCert(paths[0], paths[1]))
    {
        ret = -7;
        goto out;
    }
    
    printf("Keydata and device certificate successfully loaded.\n\n");
    
    /* Unpack input WAD package. */
    if (!wadUnpackInstallablePackage(paths[2], paths[4], cert_chain, ticket, tmd))
    {
        ret = -8;
        goto out;
    }
    
    printf("WAD package \"" OS_PRINT_STR "\" successfully unpacked.\n\n", paths[2]);
    
	/* Get TMD common block and retrieve the title ID and required system version. */
    tmd_common_block = tmdGetCommonBlock(tmd->data);
	title_id = bswap_64(tmd_common_block->title_id);
    required_ios = TITLE_LOWER(bswap_64(tmd_common_block->system_version));
	
	////////////////////////////////////////////////////////////
	//MOD Start
	////////////////////////////////////////////////////////////

	//Read help TMD file
	char tid_ascii[5] = {0};
	utilsGenerateAsciiStringFromTitleIdLower(title_id, tid_ascii);

	os_char_t path_temp[MAX_PATH];
	os_snprintf(path_temp, MAX_PATH, "./TMD/%s/tmd.bin",tid_ascii);
	FILE *file_temp = os_fopen(path_temp, OS_MODE_READ);
	
	os_fseek(file_temp, 0, SEEK_END);
    u64 size_temp = os_ftell(file_temp);
    rewind(file_temp);
	
	TitleMetadata *tmd_temp = (TitleMetadata*)calloc(1, sizeof(TitleMetadata));
	tmdReadTitleMetadataFromFile(file_temp, size_temp, tmd_temp, cert_chain);
	
	if (file_temp) fclose(file_temp);

	TmdCommonBlock *tmd_common_block_temp = tmdGetCommonBlock(tmd_temp->data);
	TmdContentRecord *tmd_contents_temp = tmdGetTitleMetadataContentRecords(tmd_common_block_temp);
	u16 content_count_temp = bswap_16(tmd_common_block_temp->content_count);
	
	//Read help TIK file
	os_snprintf(path_temp, MAX_PATH, "./TMD/%s/tik.bin",tid_ascii);
	file_temp = os_fopen(path_temp, OS_MODE_READ);
	
	os_fseek(file_temp, 0, SEEK_END);
    size_temp = os_ftell(file_temp);
    rewind(file_temp);
	
	Ticket *ticket_temp = (Ticket*)calloc(1, sizeof(Ticket));
	tikReadTicketFromFile(file_temp, size_temp, ticket_temp, cert_chain);
	if (file_temp) fclose(file_temp);
	
	//File0 SHA test
	os_snprintf(path_temp, MAX_PATH, "./wad2bin_wad_data/00000000.app");
	file_temp = os_fopen(path_temp, OS_MODE_READ);
	
	os_fseek(file_temp, 0, SEEK_END);
    size_temp = os_ftell(file_temp);
    rewind(file_temp);
	
	u8 *data_temp = (u8*)malloc(size_temp);
	fread(data_temp, 1, size_temp, file_temp);
	
	if (file_temp) fclose(file_temp);
	
	u8 hash_temp[SHA1_HASH_SIZE] = {0};
	mbedtls_sha1(data_temp,size_temp,hash_temp);
	
	if (memcmp(hash_temp, tmd_contents_temp[0].hash, SHA1_HASH_SIZE) != 0)
    {
		printf("File-0 SHA-1 not found in help TMD file!\nProcess failed!\n\n");
        goto out;
    }
	
	//Read original TMD file
	TmdContentRecord *tmd_contents = NULL;
	tmd_contents = tmdGetTitleMetadataContentRecords(tmd_common_block);
	u16 content_count = bswap_16(tmd_common_block->content_count);
	
	//File2 SHA and index test
	u16 cnt_idx = 0;
	for(u16 i = content_count-1; i > 1; i--)
	{	
		cnt_idx = bswap_16(tmd_contents[i].index);
		os_snprintf(path_temp, MAX_PATH, "./wad2bin_wad_data/%08" PRIx16 ".app", cnt_idx);
		file_temp = os_fopen(path_temp, OS_MODE_READ);
		if (file_temp) break;
	}
	
	os_fseek(file_temp, 0, SEEK_END);
    size_temp = os_ftell(file_temp);
    rewind(file_temp);
	
	data_temp = (u8*)realloc(data_temp, size_temp);
	fread(data_temp, 1, size_temp, file_temp);
	
	if (file_temp) fclose(file_temp);
	
	mbedtls_sha1(data_temp,size_temp,hash_temp);
	
	u16 number_temp = 0;
	for(u16 i = 1; i < content_count_temp; i++)
	{
		if (memcmp(hash_temp, tmd_contents_temp[i].hash, SHA1_HASH_SIZE) == 0) number_temp = i;
	}
	if ((number_temp == 0) || (cnt_idx != bswap_16(tmd_contents_temp[number_temp].index)))
	{
		printf("File-2 SHA-1 not found in help TMD file, or index is not the same!\nProcess failed!\n\n");
        goto out;
    }
	
	//File1 open
	os_snprintf(path_temp, MAX_PATH, "./wad2bin_wad_data/%08" PRIx16 ".app", cnt_idx - 1);
	file_temp = os_fopen(path_temp, OS_MODE_READ);

	os_fseek(file_temp, 0, SEEK_END);
    size_temp = os_ftell(file_temp);
    rewind(file_temp);
	
	data_temp = (u8*)realloc(data_temp, size_temp);
	fread(data_temp, 1, size_temp, file_temp);
	
	if (file_temp) fclose(file_temp);
	
	os_remove(path_temp);
	
	//File1 size comparsion
	number_temp--;
	u64 size_tmd_temp = bswap_64(tmd_contents_temp[number_temp].size);
	signed char size_diff = size_temp - size_tmd_temp;
	
	//File1 "pack_name" modification
	if (size_diff > 2)
	{
		printf("File1 pack_name modification...\n");
		u8 s[] = {0x0D,0x0A,0x20};
		u64 p1 = 0, p2 = 0;
		for (u64 i = size_temp - 2; i > size_temp - 128; i--)
		{
			if ((data_temp[i] == s[0]) && (data_temp[i+1] == s[1]) && (data_temp[i+2] == s[2]))
			{
				if (p2 == 0) p2 = i;
				else {
					p1 = i; 
					break;
				}
			}		
		}
		size_temp = size_temp - (p2 - p1);
		u8 *d = (u8*)malloc(size_temp);
		memcpy(d, data_temp, p1);
		memcpy(d + p1, data_temp + p2, size_temp - p1);
		data_temp = (u8*)realloc(data_temp, size_temp);
		memcpy(data_temp ,d ,size_temp);
	}

	//File1 resize
	size_diff = size_temp - size_tmd_temp;
	
	if ((size_diff > 0) && (size_diff < 3))
	{
		printf("File1 size is bigger than TMD size, truncating File1...\n");
		size_temp = size_temp - size_diff;
		u8 *d = (u8*)malloc(size_temp);
		memcpy(d, data_temp, size_temp);
		data_temp = (u8*)realloc(data_temp, size_temp);
		memcpy(data_temp ,d ,size_temp);
	}
	
 	//File1 main modification and SHA test
	u8 search[]={0x2F,0x73,0x5A};// search string "/sZ"
	for (u64 i = 0; i < size_temp-2; i++)
	{
		if ((data_temp[i] == search[0]) && (data_temp[i+1] == search[1]) && (data_temp[i+2] == search[2]))
		{			
			data_temp[i+4] = 0x50;
			break;
		}		
	}
	
	mbedtls_sha1(data_temp,size_temp,hash_temp);
	if (memcmp(hash_temp, tmd_contents_temp[number_temp].hash, SHA1_HASH_SIZE) == 0) goto file1save;
	
	//File1 modification 1 and SHA test
	data_temp[0] = 0x55;
	data_temp[1] = 0xAA;
	
	mbedtls_sha1(data_temp,size_temp,hash_temp);
	if (memcmp(hash_temp, tmd_contents_temp[number_temp].hash, SHA1_HASH_SIZE) == 0) goto file1save;
	
	//File1 modification 2 and SHA test
	for (u8 i = 0; i < 0xFF; i++)
	{
		data_temp[0x67] = i;
		mbedtls_sha1(data_temp,size_temp,hash_temp);
		if (memcmp(hash_temp, tmd_contents_temp[number_temp].hash, SHA1_HASH_SIZE) == 0) goto file1save;
	}
	
	//File1 modification did not helped...
	file_temp = os_fopen(path_temp, OS_MODE_WRITE);
	fwrite(data_temp,1,size_temp,file_temp);
		
	if (file_temp) fclose(file_temp);
	
	printf("File-1 SHA-1 not found in help TMD file!\nProcess failed!\n\n");
    goto out;
    
file1save:
	//File1 save
	file_temp = os_fopen(path_temp, OS_MODE_WRITE);
	fwrite(data_temp,1,size_temp,file_temp);
		
	if (file_temp) fclose(file_temp);
	
	//TMD and Ticket from help files
	tmd = tmd_temp;
	ticket = ticket_temp;

	////////////////////////////////////////////////////////////
	//MOD End
	////////////////////////////////////////////////////////////
	
	/* Start conversion process. */
    tid_upper = TITLE_UPPER(title_id);
    if (tid_upper == TITLE_TYPE_DLC)
    {
        /* Check if a parent title ID was provided. */
        if (argc < (PATH_COUNT + 2))
        {
            ERROR_MSG("Error: parent title ID not provided! This is required for DLC titles.\n");
            ret = -9;
            goto out;
        }
        
        /* Check if we're dealing with a DLC that can be converted. */
        if (!binIsDlcTitleConvertible(title_id))
        {
            ERROR_MSG("This DLC package belongs to a game that doesn't support the <index>.bin format!\nConversion process halted.\n");
            ret = -10;
            goto out;
        }
        
        /* Generate <index>.bin file(s). */
        if (!binGenerateIndexedPackagesFromUnpackedInstallableWadPackage(paths[4], paths[3], tmd, parent_tid, use_null_key))
        {
            ret = -11;
            goto out;
        }
    } else {
        /* Generate content.bin file. */
        if (!binGenerateContentBinFromUnpackedInstallableWadPackage(paths[4], paths[3], tmd))
        {
            ret = -12;
            goto out;
        }
    }
    
    /* Generate bogus installable WAD package. */
    if (!wadGenerateBogusInstallablePackage(paths[3], cert_chain, ticket, tmd))
    {
        ret = -13;
        goto out;
    }
    
    printf("Process finished!\n\n");
    
    /* Print message about needing a patched IOS. */
    if (!ticket->valid_sig || !tmd->valid_sig)
    {
        printf("The signature from the ticket/TMD in the provided WAD package isn't valid.\n");
        
        if (tid_upper == TITLE_TYPE_DLC)
        {
            if (use_null_key)
            {
                printf("In order to use the converted DLC, you'll need to install a patched IOS%u and launch the game via the Disc Channel.\n", required_ios);
                printf("The converted DLC won't work if you launch the game using a cIOS (e.g. NeoGamma, USB Loader).\n");
            } else {
                printf("In order to use the converted DLC, you'll need to launch the game using a cIOS (NeoGamma, USB Loader),\n");
                printf("or install a patched IOS%u (if you wish to use the Disc Channel).\n", required_ios);
            }
        } else {
            printf("You'll need to install a patched System Menu IOS in order to run this channel from the SD card menu.\n\n");
        }
    } else {
        printf("Both ticket/TMD signatures are valid.\n");
        
        if (tid_upper == TITLE_TYPE_DLC)
        {
            printf("This DLC should work right away on your console without having to launch the game using a cIOS, nor having to\n");
            printf("install a patched IOS%u.\n", required_ios);
        } else {
            printf("This channel should work right away on your console without having to install a patched System Menu IOS.\n\n");
        }
    }
    
    if (tid_upper == TITLE_TYPE_DLC) printf("If it doesn't work anyway, try converting the DLC %s \"" NULL_KEY_ARG "\".\n\n", (use_null_key ? "without" : "with"));
    
    printf("Remember to install the generated bogus WAD file! If it doesn't work, try uninstalling it first (for real).\n\n");
    
out:
    if (ret < 0 && ret != -1) printf("Process failed!\n\n");
    
    if (tmd)
    {
        tmdFreeTitleMetadata(tmd);
        free(tmd);
    }
    
    if (ticket)
    {
        tikFreeTicket(ticket);
        free(ticket);
    }
    
    if (cert_chain)
    {
        certFreeCertificateChain(cert_chain);
        free(cert_chain);
    }
    
    /* Remove unpacked WAD directory. */
    if (paths[4]) utilsRemoveDirectoryRecursively(paths[4]);
    
    for(u32 i = 0; i <= PATH_COUNT; i++)
    {
        if (paths[i]) free(paths[i]);
    }
    
    return ret;
}
