/**
 * Content ID
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ipfs/cid/cid.h"
#include "libp2p/crypto/encoding/base58.h"
#include "ipfs/multibase/multibase.h"
#include "mh/multihash.h"
#include "varint.h"

enum WireType ipfs_cid_message_fields[] = { WIRETYPE_VARINT, WIRETYPE_VARINT, WIRETYPE_LENGTH_DELIMITED };


size_t ipfs_cid_protobuf_encode_size(struct Cid* cid) {
	return 11+12+cid->hash_length+11;
}

int ipfs_cid_protobuf_encode(struct Cid* cid, unsigned char* buffer, size_t buffer_length, size_t* bytes_written) {
	size_t bytes_used;
	*bytes_written = 0;
	int retVal = 0;
	retVal = protobuf_encode_varint(1, ipfs_cid_message_fields[0], cid->version, buffer, buffer_length, &bytes_used);
	*bytes_written += bytes_used;
	retVal = protobuf_encode_varint(2, ipfs_cid_message_fields[1], cid->codec, &buffer[*bytes_written], buffer_length - (*bytes_written), &bytes_used);
	*bytes_written += bytes_used;
	retVal = protobuf_encode_length_delimited(3, ipfs_cid_message_fields[2], cid->hash, cid->hash_length, &buffer[*bytes_written], buffer_length - (*bytes_written), &bytes_used);
	*bytes_written += bytes_used;
	return 1;
}

int ipfs_cid_protobuf_decode(unsigned char* buffer, size_t buffer_length, struct Cid** output) {
	size_t pos = 0;
	struct Cid cid;
	int retVal = 0;
	while(pos < buffer_length) {
		size_t bytes_read = 0;
		int field_no;
		enum WireType field_type;
		if (protobuf_decode_field_and_type(&buffer[pos], buffer_length, &field_no, &field_type, &bytes_read) == 0) {
			return 0;
		}
		pos += bytes_read;
		switch(field_no) {
			case (1):
				cid.version = varint_decode(&buffer[pos], buffer_length - pos, &bytes_read);
				pos += bytes_read;
				break;
			case (2):
				cid.codec = varint_decode(&buffer[pos], buffer_length - pos, &bytes_read);
				pos += bytes_read;
				break;
			case (3):
				retVal = protobuf_decode_length_delimited(&buffer[pos], buffer_length - pos, &cid.hash, &cid.hash_length, &bytes_read);
				if (retVal == 0)
					return 0;
				pos += bytes_read;
				break;
		}

	}

	retVal = ipfs_cid_new(cid.version, cid.hash, cid.hash_length, cid.codec, output);
	free(cid.hash);
	return retVal;
}

/**
 * Create a new CID based on the given hash
 * @param version the version
 * @param hash the multihash
 * @param hash_length the length of the multihash in bytes
 * @param codec the codec to be used (NOTE: For version 0, this should be CID_PROTOBUF)
 * @param cid where to put the results
 * @returns true(1) on success
 */
int ipfs_cid_new(int version, unsigned char* hash, size_t hash_length, const char codec, struct Cid** ptrToCid) {
	// allocate memory
	*ptrToCid = (struct Cid*)malloc(sizeof(struct Cid));
	struct Cid* cid = *ptrToCid;
	if (cid == NULL)
		return 0;
	cid->hash = malloc(sizeof(unsigned char) * hash_length);
	if (cid->hash == NULL) {
		free(cid);
		return 0;
	}
	// assign values
	cid->version = version;
	cid->codec = codec;
	memcpy(cid->hash, hash, hash_length);
	cid->hash_length = hash_length;

	return 1;

}

/***
 * Free the resources from a Cid
 * @param cid the struct
 * @returns 1
 */
int ipfs_cid_free(struct Cid* cid) {
	if (cid->hash != NULL)
		free(cid->hash);
	free(cid);
	return 1;
}

/***
 * Fill a Cid struct based on a base 58 encoded string
 * @param incoming the string
 * @param incoming_size the size of the string
 * @cid the Cid struct to fill
 * @return true(1) on success
 */
int ipfs_cid_decode_from_string(const unsigned char* incoming, size_t incoming_length, struct Cid** cid) {
	int retVal = 0;

	if (incoming_length < 2)
		return 0;

	// is this a sha_256 multihash?
	if (incoming_length == 46 && incoming[0] == 'Q' && incoming[1] == 'm') {
		size_t hash_length = libp2p_crypto_encoding_base58_decode_size(incoming_length);
		unsigned char hash[hash_length];
		unsigned char* ptr = hash;
		retVal = libp2p_crypto_encoding_base58_decode(incoming, incoming_length, &ptr, &hash_length);
		if (retVal == 0)
			return 0;
		// now we have the hash, build the object
		return ipfs_cid_new(0, hash, hash_length, CID_PROTOBUF, cid);
	}

	// TODO: finish this
	/*
	// it wasn't a sha_256 multihash, try to decode it using multibase
	size_t buffer_size = multibase_decode_size(incoming_length);
	if (buffer_size == 0)
		return 0;
	unsigned char buffer[buffer_size];

	memset(buffer, 0, buffer_size);

	retVal = multibase_decode(incoming, incoming_length, buffer, buffer_size, &buffer_size);
	if (retVal == 0)
		return 0;

	return cid_cast(buffer, buffer_size, cid);
	*/
	return 0;
}

/***
 * Turn a multibase decoded string of bytes into a Cid struct
 * @param incoming the multibase decoded array
 * @param incoming_size the size of the array
 * @param cid the Cid structure to fill
 */
int ipfs_cid_cast(unsigned char* incoming, size_t incoming_size, struct Cid* cid) {
	// this is a multihash
	if (incoming_size == 34 && incoming[0] == 18 && incoming[1] == 32) {
		cid->hash_length = mh_multihash_length(incoming, incoming_size);
		cid->codec = CID_PROTOBUF;
		cid->version = 0;

		mh_multihash_digest(incoming, incoming_size, &cid->hash, &cid->hash_length);
		return 1;
	}

	// This is not a multihash. Perhaps it is using varints. Try to peel the information out of the bytes.
	// first the version
	int pos = 0;
	size_t num_bytes = 0;
	cid->version = varint_decode(&incoming[pos], incoming_size - pos, &num_bytes);
	if (num_bytes == 0 || cid->version > 1 || cid->version < 0)
		return 0;
	pos = num_bytes;
	// now the codec
	uint32_t codec = 0;
	codec = varint_decode(&incoming[pos], incoming_size - pos, &num_bytes);
	if (num_bytes == 0)
		return 0;
	cid->codec = codec;
	pos += num_bytes;
	// now what is left
	cid->hash_length = incoming_size - pos;
	cid->hash = &incoming[pos];

	return 1;
}
