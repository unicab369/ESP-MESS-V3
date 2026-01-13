#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define RECORD_FILE_BLOCK_SIZE 4096
#define HEADER_MAGIC 0xCACABABE // File identifier

static const char *TAG_RECORD = "#REC";

// ============================================================================
// HEADER STRUCTURE (stored at file start)
// ============================================================================

typedef struct __attribute__((packed)) {
	uint32_t magic;         	// header identifier
	uint32_t start_timestamp;    	// Unix timestamp of the first record
	uint32_t latest_timestamp;		// Unix timestamp of the latest record
	uint16_t next_offset;   	// Where to write next (0-4086)
	uint16_t series_count;  	// How many records written

	uint32_t preserved1;
	uint32_t preserved2;
	uint32_t preserved3;
	uint32_t preserved4;
} file_header_t;

#define HEADER_SIZE sizeof(file_header_t)  // 12 bytes
#define MAX_DATA_SIZE (RECORD_FILE_BLOCK_SIZE - HEADER_SIZE)


// ============================================================================
// CREATE/INITIALIZE FILE
// ============================================================================

void series_get_header(const char* filename, file_header_t *header) {
	FILE* f = fopen(filename, "rb");
	fread(header, 1, HEADER_SIZE, f);
	fclose(f);
}

FILE* series_file_ensure(const char* filename, file_header_t *header) {
	const char method_name[] = "series_file_start";

	// First check if file already exists and is valid
	FILE* file = fopen(filename, "rb+");			// ~7.5ms

	if (file) {
		// File exists - check if it's already a valid fixed file
		if (fread(header, 1, HEADER_SIZE, file) == HEADER_SIZE &&
			header->magic == HEADER_MAGIC
		) {
			// Valid file already exists!
			ESP_LOGI(TAG_RECORD, "%s RECORD-FOUND", method_name);
			printf("- Found: %s (%d records, next_offset %d)\n",
					filename, header->series_count, header->next_offset);
			return file;
		}

        // Invalid file - close and recreate
        fclose(file);
		ESP_LOGI(TAG_RECORD, "%s INVALID-FOUND %s. Recreating...", method_name, filename);
	}

	// Create the file
	file = fopen(filename, "wb+");
	if (!file) {
		ESP_LOGE(TAG_RECORD, "%s CANNOT-CREATE", method_name);
		printf("- Failed to create: %s\n", filename);
		return NULL;
	}

	// Write header
	header->magic = HEADER_MAGIC;
	header->start_timestamp = 0;
	header->latest_timestamp = 0;
	header->next_offset = 0;
	header->series_count = 0;
	fwrite(header, 1, HEADER_SIZE, file);

    // Pre-allocate fixed size efficiently
    uint8_t zero[512] = {0};  // Buffer for faster zero-fill
    size_t remaining = RECORD_FILE_BLOCK_SIZE - HEADER_SIZE;
    while (remaining > 0) {
        size_t chunk = (remaining > sizeof(zero)) ? sizeof(zero) : remaining;
        fwrite(zero, 1, chunk, file);
        remaining -= chunk;
    }

	// fclose(f);
	ESP_LOGI(TAG_RECORD, "%s LOG-CREATED", method_name);
	printf("- File created: %s (%d bytes)\n", filename, RECORD_FILE_BLOCK_SIZE);

	// return 1;
	return file;
}


// ============================================================================
// WRITE MULTIPLE RECORD
// ============================================================================
// assume at least one record is always inserted

int series_batch_insert(
	const char* filename, file_header_t *output_header,
	const void *records, size_t record_size, int count
) {
	const char method_name[] = "series_batch_insert";

	file_header_t current_header;
	FILE* f = series_file_ensure(filename, &current_header);
	if (!f) return 0;

	// Check capacity
	size_t total_bytes = record_size * count;
	size_t write_pos = HEADER_SIZE + output_header->next_offset;

	// // Handle wrap-around if needed
	// if (output_header->next_offset + total_bytes > MAX_DATA_SIZE) {
	//     // Wrap to beginning (circular buffer)
	//     output_header->next_offset = 0;
	//     write_pos = HEADER_SIZE;
	//     output_header->series_count = 0;  // Reset count on wrap
	// }

	// Write the records
	fseek(f, write_pos, SEEK_SET);								// ~200us
	size_t written = fwrite(records, record_size, count, f);	// ~75us

	if (written == 0) {
		// Complete failure
		fclose(f);
		ESP_LOGE(TAG_RECORD, "%s WRITE-FAILED 0/%d records written",
				method_name, count);
		return 0;
	}

	//# Update timestamps
	if (current_header.start_timestamp == 0) {
		current_header.start_timestamp = output_header->latest_timestamp;
	}
	current_header.latest_timestamp = output_header->latest_timestamp;

	//# Update headers
	size_t actual_bytes = written * record_size;
	current_header.next_offset += actual_bytes;
	current_header.series_count += (uint32_t)written;
	*output_header = current_header;

	// Write back updated header
	fseek(f, 0, SEEK_SET);							// ~150us
	fwrite(&current_header, 1, HEADER_SIZE, f);		// ~30us
	fclose(f);

	ESP_LOGI(TAG_RECORD, "%s INSERT-RECORD", method_name);
	printf("- Inserted: %d/%d records (total %d, next_offset %d)\n",
			written, count, current_header.series_count, current_header.next_offset);

	return current_header.next_offset;
}

int series_file_read(
	file_header_t *header, const char* filename,
	void* output, size_t record_size, int max_records
) {
	const char method_name[] = "series_file_read";

	FILE* file = fopen(filename, "rb");
	if (!file) {
		ESP_LOGE(TAG_RECORD, "%s NOT-FOUND", method_name);
		printf("- File not found: %s\n", filename);
		return 0;
	}

	// Read and validate current header
	fread(header, 1, HEADER_SIZE, file);
	if (header->magic != HEADER_MAGIC) {
		ESP_LOGE(TAG_RECORD, "%s INVALID-HEADER", method_name);
		fclose(file);
		return 0;
	}

	// Determine how many records to read
	int records_to_read = header->series_count;
	if (records_to_read > max_records) records_to_read = max_records;

	if (records_to_read == 0) {
		ESP_LOGI(TAG_RECORD, "%s NO-RECORD found", method_name);
        fclose(file);
        return 0;  // No records to read
    }

	// Skip header and read all records in one go
	fseek(file, HEADER_SIZE, SEEK_SET);
	int count = fread(output, record_size, records_to_read, file);
	fclose(file);

	ESP_LOGI(TAG_RECORD, "%s READ-RECORD", method_name);
	printf("- Read Completed: %d/%d records\n", count, records_to_read);

	return count;
}

// ============================================================================
// READ SPECIFIC RECORD
// ============================================================================

int series_file_read_at(
	int record_index, const char* filename, void* output, size_t record_size
) {
	const char method_name[] = "series_file_read_at";

	FILE* f = fopen(filename, "rb");
	if (!f) {
		ESP_LOGE(TAG_RECORD, "%s NOT-FOUND", method_name);
		printf("- File not found: %s\n", filename);
		return 0;
	}

	// Read header
	file_header_t header;
	fread(&header, 1, HEADER_SIZE, f);

	// Validate index
	if (record_index < 0 || record_index >= header.series_count) {
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

int series_file_read_last(const char* filename, void* output, size_t record_size) {
	const char method_name[] = "series_file_read_last";
	FILE* f = fopen(filename, "rb");
	if (!f) {
		ESP_LOGE(TAG_RECORD, "%s NOT-FOUND", method_name);
		printf("- File not found: %s\n", filename);
		return 0;
	}

	// Read header
	file_header_t header;
	fread(&header, 1, HEADER_SIZE, f);

	// Check if any records exist
	if (header.series_count == 0) {
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

void series_file_status(const char* filename, size_t record_size) {
	const char method_name[] = "series_file_status";
	FILE* f = fopen(filename, "rb");
	if (!f) {
		ESP_LOGE(TAG_RECORD, "%s NOT-FOUND", method_name);
		printf("- File not found: %s\n", filename);
		return;
	}

	file_header_t header;
	fread(&header, 1, HEADER_SIZE, f);
	fclose(f);

	uint16_t next_offset = header.next_offset;
	ESP_LOGI(TAG_RECORD, "File status for %s", filename);
	printf("Magic: 0x%08lX %s\n", header.magic,
				header.magic == HEADER_MAGIC ? "(OK)" : "(CORRUPT!)");
	printf("Records written: %d, remaining: %d\n",
				header.series_count, (MAX_DATA_SIZE - next_offset) / record_size);
	printf("Space used: %d/%d bytes\n", HEADER_SIZE + next_offset, RECORD_FILE_BLOCK_SIZE);
}