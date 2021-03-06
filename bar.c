#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

typedef struct {
    uint8_t signature[0x20];
} caf_header_signature_t;
//32

typedef struct {
    uint64_t index;
    uint8_t signature[0x20];
    uint64_t padding;
} caf_segment_signature_t;
//48

typedef struct {
    uint64_t index;
    uint64_t data_offset;
    uint64_t data_size_with_padding;
    uint64_t algorithm;
    uint64_t cipher_key_index;
    uint8_t cipher_seed[0x10];
    uint64_t data_size_without_padding;
} caf_segment_table_t;
//64

typedef struct {
    uint8_t magic[8];
    uint64_t version;
    uint64_t hasher_key_index;
    uint64_t num_segments;
    uint64_t file_offset;
    uint64_t file_size;
} caf_header_t;
//48

static const uint8_t sbl_bar_hash_key[32] = {
	0x1f, 0x18, 0xc9, 0x70, 0xd0, 0x00, 0xac, 0x7e, 
	0x6f, 0xcc, 0x1a, 0x8c, 0xdd, 0x89, 0xb4, 0xfe, 
	0xcd, 0xa1, 0x33, 0xa1, 0x0e, 0xc8, 0xf5, 0x25,
	0x98, 0x22, 0x23, 0xf5, 0x86, 0x1f, 0x02, 0x00
};

static const uint8_t sbl_bar_cipher_key[16] = {
    0x79, 0xc8, 0xcc, 0xc8, 0x89, 0xa1, 0x54, 0x0d,
    0x4f, 0x2e, 0x27, 0xbb, 0x61, 0x4f, 0xd6, 0x53
};

#define MAX_SEG_SIZE 4294901760

int last_open = -1;
FILE* f_last_open = NULL;
FILE* getArchive(uint64_t offset)
{
	char* name;
	int number = floor(offset / MAX_SEG_SIZE);

	if(number == last_open)
		return f_last_open;
	else if (last_open != -1)
		fclose(f_last_open);

	if(number == 0)
	{
		name = (char*) malloc(13);
		strcpy(name, "archive.dat");
	}
	else
	{
		name = (char*) malloc(17);
		sprintf(name, "archive%04d.dat", number);
	}
	fprintf(stderr, "reading %s\n", name);
	f_last_open = fopen(name,"rb");
	last_open = number;

	free(name);

	return f_last_open;
}

unsigned char* hmac_sha256(const void *key, int keylen,
                           const unsigned char *data, int datalen,
                           unsigned char *result, unsigned int* resultlen)
{
    return HMAC(EVP_sha256(), key, keylen, data, datalen, result, resultlen);
}

void hexDump(const void *data, size_t size) {
  size_t i;
  for (i = 0; i < size; i++) {
    printf("%02hhX%c", ((char *)data)[i], (i + 1) % 16 ? ' ' : '\n');
  }
  printf("\n");
}

#define MAX_CHUNK_SIZE 0x10000

FILE *fl = NULL;
//flatz's algo
uint8_t * cbc_dec(const unsigned char *key, unsigned char* iv, unsigned char* data, uint64_t data_size){
	
	uint8_t* result = NULL;
	//printf("data size: 0x%llx\n",data_size);
	if(data_size == 0){
		return result;
	}
	result = (uint8_t*) malloc(data_size);
	uint64_t num_data_left = data_size;
	
	uint64_t block_size = 16;
	uint64_t offset = 0;
	unsigned char *input = (unsigned char*) malloc (block_size);
	unsigned char *output = (unsigned char*) malloc (block_size);
	while(num_data_left >= block_size){
		//AES_ecb_encrypt(const unsigned char *in, unsigned char *out, const AES_KEY *key, const int enc)
		
		memcpy(input,data+offset,block_size);
		
		AES_KEY ctx;
		AES_set_decrypt_key(key,0x80,&ctx);
		AES_ecb_encrypt(input,output,&ctx,AES_DECRYPT);
		unsigned int i = 0;
		for(i=0;i<block_size;i++){
			output[i]=output[i]^iv[i];
		}
		memcpy(result+offset,output,block_size);
		memcpy(iv,input,block_size);
		num_data_left -= block_size;
		offset += block_size;
	}
	
	if (num_data_left > 0 & num_data_left < block_size){
			//printf("data left : 0x%08x\n",num_data_left);
			memcpy(input,data+offset - block_size,block_size);
			AES_KEY ctx;
			AES_set_encrypt_key(key,0x80,&ctx);
			AES_ecb_encrypt(input,output,&ctx,AES_ENCRYPT);
			unsigned int i = 0;
			for(i=0;i<num_data_left;i++){
				result[offset+i]=data[offset+i]^output[i];
			}
		}
	
    
	return result;
}

uint8_t * cbc_enc(const unsigned char *key, unsigned char* iv, unsigned char* data, uint64_t data_size){
	
	uint8_t* result = NULL;
	//printf("data size: 0x%llx\n",data_size);
	if(data_size == 0){
		return result;
	}
	result = (uint8_t*) malloc(data_size);
	uint64_t num_data_left = data_size;
	
	uint64_t block_size = 16;
	uint64_t offset = 0;
	unsigned char *input = (unsigned char*) malloc (block_size);
	unsigned char *output = (unsigned char*) malloc (block_size);
	while(num_data_left >= block_size){
		//AES_ecb_encrypt(const unsigned char *in, unsigned char *out, const AES_KEY *key, const int enc)
		unsigned int i = 0;
		for(i=0;i<block_size;i++){
			input[i]=data[offset+i]^iv[i];
		}


		AES_KEY ctx;
		AES_set_encrypt_key(key,0x80,&ctx);
		AES_ecb_encrypt(input,output,&ctx,AES_ENCRYPT);

		memcpy(result+offset,output,block_size);
		memcpy(iv,output,block_size);
		
		num_data_left -= block_size;
		offset += block_size;

	}
	
	if (num_data_left > 0 & num_data_left < block_size){
			//printf("data left : 0x%08x\n",num_data_left);
			memcpy(input,result+offset - block_size,block_size);
			AES_KEY ctx;
			AES_set_encrypt_key(key,0x80,&ctx);
			AES_ecb_encrypt(input,output,&ctx,AES_ENCRYPT);
			unsigned int i = 0;
			for(i=0;i<num_data_left;i++){
				result[offset+i]=data[offset+i]^output[i];
			}
		}
	
    
	return result;
}

int main(int argc, char** argv){
	FILE *fp=fopen("archive.dat","rb");
	fseeko(fp,0,SEEK_SET);
	unsigned char buf[48] = {0};
	fread(buf,48,1,fp);
	caf_header_t* hdr = (caf_header_t*)buf;
	printf("Number of Entries:%lx\n",hdr->num_segments);
	uint64_t i=0;
	
	
	for(i=0;i<hdr->num_segments;i++){
		uint8_t* name = (uint8_t*) malloc(10 + 3);
		sprintf(name, "blob%lx.bin", i);
		FILE *blob = fopen(name,"wb");
		uint8_t buf2[64] = {0};
		fseeko(fp,48+(64*i),SEEK_SET);
		fread(buf2,64,1,fp);
		caf_segment_table_t * seg = (caf_segment_table_t *) buf2;
		
		fl = getArchive(seg->data_offset);
		fseeko(fl,seg->data_offset % MAX_SEG_SIZE,SEEK_SET);
		
		//printf("allocating data offset 0x%lx, data size without padding 0x%lx\n",seg->data_offset,seg->data_size_without_padding);
		
		
		
		
		//HASHER
		//skipping this
		/*
		uint8_t md[0x20] = {0};
		int resultlen = 0x20;
		uint8_t buf4[48] = {0};
		fseeko(fp,48+(64*hdr->num_segments)+(48*i),SEEK_SET);
		fread(buf4,48,1,fp);
		caf_segment_signature_t * sig = (caf_segment_signature_t *) buf4;
		hmac_sha256(sbl_bar_hash_key,0x20,buf3,seg->data_size_without_padding,md,&resultlen);
		if(memcmp(md,sig->signature,0x20)==0){
			//printf("match \n");
		}else{
			hexDump(sig->signature,0x20);
		}*/
		
		uint64_t j=seg->data_size_without_padding;
		//CIPHER
		while(j >= MAX_CHUNK_SIZE){
			uint8_t* buf3 = (uint8_t*) malloc(MAX_CHUNK_SIZE);
			fread(buf3, MAX_CHUNK_SIZE, 1,fl);
			uint8_t* buf6 = cbc_dec(sbl_bar_cipher_key,seg->cipher_seed,buf3,MAX_CHUNK_SIZE);
			//printf("writing data offset %08X, data size without padding %08X\n",seg->data_offset,seg->data_size_without_padding);
			fwrite(buf6, MAX_CHUNK_SIZE, 1,blob);
			//printf("write data offset %08X, data size without padding %08X done\n",seg->data_offset,seg->data_size_without_padding);
			free(buf3);
			free(buf6);
			j-=MAX_CHUNK_SIZE;
		}if(j<MAX_CHUNK_SIZE){
			uint8_t* buf3 = (uint8_t*) malloc(j);
			fread(buf3, j, 1,fl);
			uint8_t* buf6 = cbc_dec(sbl_bar_cipher_key,seg->cipher_seed,buf3,j);
			//printf("writing data offset %08X, data size without padding %08X\n",seg->data_offset,seg->data_size_without_padding);
			fwrite(buf6, j, 1,blob);
			//printf("write data offset %08X, data size without padding %08X done\n",seg->data_offset,seg->data_size_without_padding);
			free(buf3);
			free(buf6);
		}
		fclose(blob);
		free(name);
		
	}
	fclose(fl);
	
	//let's skip this as well
	//HEADER HASHER
	/*
	fseeko(fp,48+(64*hdr->num_segments)+(48*hdr->num_segments),SEEK_SET);
	uint64_t header_size=ftello(fp);
	fseeko(fp,0,SEEK_SET);
	uint8_t *buf5 = (uint8_t *) malloc(header_size);
	fread(buf5,header_size,1,fp);
	uint8_t md2[0x20] = {0};
	int resultlen2 = 0x20;
	hmac_sha256(sbl_bar_hash_key,0x20,buf5,header_size,md2,&resultlen2);
	uint8_t header_hash[0x20]={0};
	fseeko(fp,48+(64*hdr->num_segments)+(48*hdr->num_segments),SEEK_SET);
	fread(header_hash,0x20,1,fp);
	
	if(memcmp(header_hash,md2,0x20)==0){
		//printf("header_hash match\n");
	}
	*/
	fclose(fp);
	
	return 0;
}
