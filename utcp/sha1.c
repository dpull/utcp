#include "3rd/WjCryptLib_Sha1.h"
#include "utcp_utils.h"
#include <assert.h>
#include <string.h>

/**
 * Calculate the hash on a single block and return it
 *
 * @param Data Input data to hash
 * @param DataSize Size of the Data block
 * @param OutHash Resulting hash value (20 byte buffer)
 */
// FSHA1::HashBuffer
static void hash_buffer(const void* Data, uint64_t DataSize, uint8_t* OutHash)
{
	// do an atomic hash operation
	Sha1Context sha1Context;
	SHA1_HASH* sha1Hash = (SHA1_HASH*)OutHash;

	Sha1Initialise(&sha1Context);
	Sha1Update(&sha1Context, Data, (uint32_t)DataSize);
	Sha1Finalise(&sha1Context, sha1Hash);
}

/**
 * Generate the HMAC (Hash-based Message Authentication Code) for a block of data.
 * https://en.wikipedia.org/wiki/Hash-based_message_authentication_code
 *
 * @param Key		The secret key to be used when generating the HMAC
 * @param KeySize	The size of the key
 * @param Data		Input data to hash
 * @param DataSize	Size of the Data block
 * @param OutHash	Resulting hash value (20 byte buffer)
 */

// FSHA1::HMACBuffer
void sha1_hmac_buffer(const void* Key, uint32_t KeySize, const void* Data, uint64_t DataSize, uint8_t* OutHash)
{
	enum
	{
		BlockSize = 64,
		HashSize = 20,
	};

	uint8_t FinalKey[BlockSize];

	// Fit 'Key' into a BlockSize-aligned 'FinalKey' value
	if (KeySize > BlockSize)
	{
		hash_buffer(Key, KeySize, FinalKey);

		memset(FinalKey + HashSize, 0, BlockSize - HashSize);
	}
	else if (KeySize < BlockSize)
	{
		memcpy(FinalKey, Key, KeySize);
		memset(FinalKey + KeySize, 0, BlockSize - KeySize);
	}
	else
	{
		memcpy(FinalKey, Key, KeySize);
	}

	uint8_t OKeyPad[BlockSize];
	uint8_t IKeyPad[BlockSize];

	for (int32_t i = 0; i < BlockSize; i++)
	{
		OKeyPad[i] = 0x5C ^ FinalKey[i];
		IKeyPad[i] = 0x36 ^ FinalKey[i];
	}

	// Start concatenating/hashing the pads/data etc: Hash(OKeyPad + Hash(IKeyPad + Data))
	//_countof(IKeyPad) + DataSize
	uint8_t IKeyPad_Data[1024];
	assert(_countof(IKeyPad_Data) >= _countof(IKeyPad) + DataSize);

	memcpy(IKeyPad_Data, IKeyPad, _countof(IKeyPad));
	memcpy(IKeyPad_Data + _countof(IKeyPad), Data, DataSize);

	uint8_t IKeyPad_Data_Hash[HashSize];

	hash_buffer(IKeyPad_Data, _countof(IKeyPad) + DataSize, IKeyPad_Data_Hash);

	uint8_t OKeyPad_IHash[_countof(OKeyPad) + HashSize];

	memcpy(OKeyPad_IHash, OKeyPad, _countof(OKeyPad));
	memcpy(OKeyPad_IHash + _countof(OKeyPad), IKeyPad_Data_Hash, HashSize);

	// Output the final hash
	hash_buffer(OKeyPad_IHash, _countof(OKeyPad_IHash), OutHash);
}
