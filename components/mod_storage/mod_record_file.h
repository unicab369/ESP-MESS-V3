#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define BLOCK_SIZE 4096
#define HEADER_MAGIC 0xCACABABE // File identifier

static const char *TAG_RECORD = "#REC";

// ============================================================================
// HEADER STRUCTURE (stored at file start)
// ============================================================================
typedef struct __attribute__((packed)) {
	uint32_t magic;          // Always 0xCACABABE
	uint16_t next_offset;    // Where to write next (0-4086)
	uint16_t record_count;   // How many records written
	uint32_t checksum;       // Optional: for data integrity
} file_header_t;

#define HEADER_SIZE sizeof(file_header_t)  // 12 bytes
#define MAX_DATA_SIZE (BLOCK_SIZE - HEADER_SIZE)


// ============================================================================
// CREATE/INITIALIZE FILE
// ============================================================================

int record_file_start(const char* filename) {
	// First check if file already exists and is valid
	FILE* test = fopen(filename, "rb");
	if (test) {
		// File exists - check if it's already a valid fixed file
		file_header_t existing_header;

		if (fread(&existing_header, 1, HEADER_SIZE, test) == HEADER_SIZE &&
			existing_header.magic == HEADER_MAGIC
		) {
			// Valid file already exists!
			fclose(test);
			ESP_LOGW(TAG_RECORD, "record_file_start %s already exists (has %d records)", 
						filename, existing_header.record_count);
			return 1;  // Success - file already good
		}
		fclose(test);
		
		// File exists but is invalid/corrupted
		ESP_LOGW(TAG_RECORD, "record_file_start %s is invalid/corrupted. Recreating...", filename);
	}

	// Create the file
	FILE* f = fopen(filename, "wb");
	if (!f) {
		ESP_LOGW(TAG_RECORD, "Failed to create %s", filename);
		return 0;
	}
	
	// Write header
	file_header_t header = {
		.magic = HEADER_MAGIC,
		.next_offset = 0,
		.record_count = 0,
		.checksum = 0
	};
	fwrite(&header, 1, HEADER_SIZE, f);
	
	// Fill rest with zeros (pre-allocate)
	uint8_t zero = 0;
	for (int i = HEADER_SIZE; i < BLOCK_SIZE; i++) {
		fwrite(&zero, 1, 1, f);
	}
	
	fclose(f);
	ESP_LOGW(TAG_RECORD, "record_file_start Created %s (%d bytes) with header",
			filename, BLOCK_SIZE);
	return 1;
}


// ============================================================================
// WRITE MULTIPLE RECORD
// ============================================================================

int record_file_write(
	const char* filename, const void *records, size_t record_size, int count
) {
	FILE* f = fopen(filename, "rb+");
	if (!f) return 0;
	// Read current header
	file_header_t header;
	fread(&header, 1, HEADER_SIZE, f);
	
	// Validate file
	if (header.magic != HEADER_MAGIC) {
		fclose(f);
		ESP_LOGE(TAG_RECORD, "record_file_write Invalid format - header identifier!");
		return 0;
	}

	// Check capacity
	size_t total_bytes = record_size * count;
	if (header.next_offset + total_bytes > MAX_DATA_SIZE) {
		fclose(f);
		ESP_LOGE(TAG_RECORD, "record_file_write Not enough space! %d records written.",
				header.record_count);
		return 0;
	}
	
	// Write each record
    size_t write_pos = HEADER_SIZE + header.next_offset;
    fseek(f, write_pos, SEEK_SET);
    size_t written = fwrite(records, record_size, count, f);
    
    if (written != count) {
        ESP_LOGE(TAG_RECORD, "record_file_write Partial write: %zu/%d records", written, count);
        fclose(f);
        return 0;
    }
	
	// Update header
    header.next_offset += total_bytes;
    header.record_count += count;
	
	// Write back updated header
	fseek(f, 0, SEEK_SET);
	fwrite(&header, 1, HEADER_SIZE, f);
	fclose(f);
	ESP_LOGW(TAG_RECORD, "record_file_write Wrote %d record (current count: %d @ offset %d)", 
		count, header.record_count, header.next_offset - record_size);
	return 1;
}


// ============================================================================
// WRITE NEXT RECORD
// ============================================================================

int record_file_insert(const char* filename, const void *record, size_t record_size) {
	return record_file_write(filename, record, record_size, 1);
}


int record_file_read(
	file_header_t *header, const char* filename,
	void* output, size_t record_size, int max_records
) {
	FILE* f = fopen(filename, "rb");
	if (!f) return 0;
	// Read current header
	fread(header, 1, HEADER_SIZE, f);

	// Validate
	if (header->magic != HEADER_MAGIC) {
		fclose(f);
		return 0;
	}

	// Determine how many records to read
	int records_to_read = header->record_count;
	if (records_to_read > max_records) records_to_read = max_records;
	
	if (records_to_read == 0) {
        fclose(f);
        return 0;  // No records to read
    }

	// Skip header and read all records in one go
	fseek(f, HEADER_SIZE, SEEK_SET);
	int count = fread(output, record_size, header->record_count, f);
	fclose(f);
	return count;
}

// ============================================================================
// READ SPECIFIC RECORD
// ============================================================================

int record_file_read_at(
	int record_index, const char* filename, void* output, size_t record_size
) {
	FILE* f = fopen(filename, "rb");
	if (!f) {
		ESP_LOGE(TAG_RECORD, "Err record_file_read_at not found: %s", filename);
		return 0;
	}
	
	// Read header
	file_header_t header;
	fread(&header, 1, HEADER_SIZE, f);

	// Validate index
	if (record_index < 0 || record_index >= header.record_count) {
		fclose(f);
		return 0;
	}
	
	// Calculate position: header + (index * record_size) & Read the record
	size_t read_pos = HEADER_SIZE + (record_index * record_size);
	fseek(f, read_pos, SEEK_SET);
	int result = fread(output, 1, record_size, f);
	fclose(f);
	return (result == record_size);
}


// ============================================================================
// GET LAST RECORD
// ============================================================================

int record_file_read_last(const char* filename, void* output, size_t record_size) {
	FILE* f = fopen(filename, "rb");
	if (!f) {
		ESP_LOGE(TAG_RECORD, "Err record_file_read_last not found: %s", filename);
		return 0;
	}
	
	// Read header
	file_header_t header;
	fread(&header, 1, HEADER_SIZE, f);

	// Check if any records exist
	if (header.record_count == 0) {
		fclose(f);
		return 0;
	}
	
	// Last record is at: header + next_offset - record_size
	size_t last_pos = HEADER_SIZE + header.next_offset - record_size;
	fseek(f, last_pos, SEEK_SET);
	int result = fread(output, 1, record_size, f);
	fclose(f);
	return (result == record_size);
}


// ============================================================================
// GET FILE STATUS
// ============================================================================

void record_file_status(const char* filename, size_t record_size) {
	FILE* f = fopen(filename, "rb");
	if (!f) {
		ESP_LOGE(TAG_RECORD, "Err record_file_status not found: %s", filename);
		return;
	}
	
	file_header_t header;
	fread(&header, 1, HEADER_SIZE, f);
	fclose(f);
	
    uint16_t next_offset = header.next_offset;
	ESP_LOGW(TAG_RECORD, "File status for %s", filename);
	printf("Magic: 0x%08lX %s\n", header.magic, 
				header.magic == HEADER_MAGIC ? "(OK)" : "(CORRUPT!)");
	printf("Records written: %d, remaining: %d\n",
                header.record_count, (MAX_DATA_SIZE - next_offset) / record_size);
	printf("Space used: %d/%d bytes\n", HEADER_SIZE + next_offset, BLOCK_SIZE);
}