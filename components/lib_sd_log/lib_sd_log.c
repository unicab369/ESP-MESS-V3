#include "lib_sd_log.h"
#include "esp_timer.h"

#define ROTATE_LOG_FILE_COUNT 2
#define MOUNT_POINT_LOG "/sdcard"

rotate_log_t system_log = {
	.file = NULL,
	.file_num = 0,
	.lines = 0,
	.prefix = "sys",
	.CLOSE_LINES = 20,
	.MAX_LINES = 200
};

rotate_log_t error_log = {
	.file = NULL,
	.file_num = 0,
	.lines = 0,
	.prefix = "err",
	.CLOSE_LINES = 5,
	.MAX_LINES = 200
};

// why: rotate log between 2 files, prevent opening file too many times because it is slow
// fclose is required for the log to be written properly, so here we close the log file every x lines
// and rotate to the next file when we reach the maximum specified number of lines

void rotate_log_close(rotate_log_t *log) {
	if (log->file) fclose(log->file);
	log->file = NULL;
}

void rotate_log_write(rotate_log_t *log, const char *msg) {
	// Close every x lines
	if (log->lines % log->CLOSE_LINES == 0) {
		if (log->file) fclose(log->file);
		
		// Rotate to next file every x lines
		if (log->lines >= log->MAX_LINES) {
			log->file_num = (log->file_num + 1) % ROTATE_LOG_FILE_COUNT;
			log->lines = 0;
		}
		
		// Open current file
		char path[64];
		snprintf(path, sizeof(path), MOUNT_POINT_LOG"/log/%s_%d.txt", log->prefix, log->file_num);
		log->file = fopen(path, log->lines == 0 ? "w" : "a");

		if (!log->file) {
			ESP_LOGE("[SD_LOG]", "Failed to open rotate_log: %s", path);
			return;
		}
	}

	// Write
	if (log->file) {
		fputs(msg, log->file);
		fputc('\n', log->file);
		log->lines++;
	}
}

// read logs from the last 2 files up to the maximum specified size

size_t rotate_log_get_latest(rotate_log_t *log, char *buffer, size_t buffer_size) {
    // Input validation
    if (!buffer || buffer_size < 2) {
        if (buffer && buffer_size > 0) buffer[0] = '\0';
        return 0;
    }

	buffer[0] = '\0';	// Clear buffer
	size_t total = 0;

	// File 1:  Read from current file
	char path[64];
	snprintf(path, sizeof(path), MOUNT_POINT_LOG"/log/%s_%d.txt", log->prefix, log->file_num);

	FILE *f = fopen(path, "r");
	if (f) {
		// Get file size first
		fseek(f, 0, SEEK_END);
		long size = ftell(f);
		
		// Safe seek (don't seek before file start)
		size_t to_read = (size < buffer_size - 1) ? size : buffer_size - 1;

		if (to_read > 0) {
			fseek(f, -to_read, SEEK_END);
			total = fread(buffer, 1, to_read, f);
		}
		fclose(f);
	}

	// File 2: Previous 
	if (total < buffer_size && ROTATE_LOG_FILE_COUNT > 1) {
		int prev = (log->file_num - 1 + ROTATE_LOG_FILE_COUNT) % ROTATE_LOG_FILE_COUNT;

		snprintf(path, sizeof(path), MOUNT_POINT_LOG"/log/%s_%d.txt", log->prefix, prev);
		f = fopen(path, "r");

		if (f) {
			fseek(f, 0, SEEK_END);
			long size = ftell(f);
			
			size_t need = buffer_size - total - 1;  // -1 for null terminator
			size_t to_read = (size < need) ? size : need;
			
			if (to_read > 0) {
				fseek(f, -to_read, SEEK_END);
				total += fread(buffer + total, 1, to_read, f);
			}
			fclose(f);
		}
	}

	buffer[total] = '\0';
	return total;
}

void log_to_sd(rotate_log_t *log, const char *tag, const char *format, ...) {
	char log_line[160];
	int64_t runtime_ms = esp_timer_get_time() / 1000;
	int written = snprintf(log_line, sizeof(log_line), "[%lld] %s: ", runtime_ms, tag);

	if (written > 0 && written < sizeof(log_line)) {
		va_list args;
		va_start(args, format);
		vsnprintf(log_line + written, sizeof(log_line) - written, format, args);
		va_end(args);
	}

	rotate_log_write(log, log_line);
}