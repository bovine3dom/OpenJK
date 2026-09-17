//
// Saved game.
//


#include "ojk_saved_game.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>
#include "ojk_saved_game_helper.h"
#include "qcommon/qcommon.h"
#include "server/server.h"


namespace ojk
{


SavedGame::SavedGame() :
		error_message_(),
		file_handle_(),
		version_(),
		io_buffer_(),
		saved_io_buffer_(),
		io_buffer_offset_(),
		saved_io_buffer_offset_(),
		rle_buffer_(),
		chunks_(),
		write_thread_(),
		write_mutex_(),
		write_condition_(),
		write_jobs_(),
		write_results_(),
		write_worker_busy_(),
		stop_write_worker_(),
		is_readable_(),
		is_writable_(),
		is_failed_()
{
}

SavedGame::~SavedGame()
{
	close();

	{
		std::lock_guard<std::mutex> lock(write_mutex_);
		stop_write_worker_ = true;
	}
	write_condition_.notify_one();
	if (write_thread_.joinable())
	{
		write_thread_.join();
	}
}

bool SavedGame::open(
	const std::string& base_file_name)
{
	close();


	const std::string file_path = generate_path(
		base_file_name);

	bool is_succeed = true;

	static_cast<void>(::FS_FOpenFileRead(
		file_path.c_str(),
		&file_handle_,
		qtrue));

	if (file_handle_ == 0)
	{
		is_succeed = false;

		error_message_ =
			S_COLOR_RED "Failed to open a saved game file: \"" +
			file_path + "\".";

		::Com_DPrintf(
			"%s\n",
			error_message_.c_str());
	}

	if (is_succeed)
	{
		is_readable_ = true;
	}


	if (is_succeed)
	{
		SavedGameHelper saved_game(
			this);

		int sg_version = -1;

		if (saved_game.try_read_chunk<int32_t>(
			INT_ID('_', 'V', 'E', 'R'),
			sg_version))
		{
			if (sg_version < 1 || sg_version > iSAVEGAME_VERSION)
			{
				is_succeed = false;

				::Com_Printf(
					S_COLOR_RED "File \"%s\" has unsupported version # %d (current %d)\n",
					base_file_name.c_str(),
					sg_version,
					iSAVEGAME_VERSION);
			}
			else
			{
				version_ = sg_version;
			}
		}
		else
		{
			is_succeed = false;

			::Com_Printf(
				S_COLOR_RED "Failed to read a version.\n");
		}
	}

	if (!is_succeed)
	{
		close();
	}

	return is_succeed;
}

bool SavedGame::create()
{
	close();

	is_writable_ = true;
	version_ = iSAVEGAME_VERSION;

	SavedGameHelper sgsh(this);

	sgsh.write_chunk<int32_t>(
		INT_ID('_', 'V', 'E', 'R'),
		version_);

	if (is_failed())
	{
		close();
		return false;
	}

	return true;
}

bool SavedGame::finish_write(
	const std::string& base_file_name,
	const std::vector<std::string>& rotation)
{
	if (!is_writable_ || is_failed_)
	{
		return false;
	}

	WriteJob job;
	job.name = base_file_name;
	job.compress = (::sv_compress_saved_games->integer != 0);

	if (!get_write_path(base_file_name, job.target_path))
	{
		return false;
	}
	job.temporary_path = job.target_path + ".tmp";

	for (const std::string& name : rotation)
	{
		std::string path;
		if (!get_write_path(name, path))
		{
			return false;
		}
		job.rotation_paths.push_back(path);
	}

	job.chunks.swap(chunks_);
	version_ = 0;
	clear_error();
	reset_buffer();
	is_writable_ = false;

	{
		std::lock_guard<std::mutex> lock(write_mutex_);
		if (!write_thread_.joinable())
		{
			write_thread_ = std::thread(&SavedGame::write_worker, this);
		}
		write_jobs_.push_back(std::move(job));
	}
	write_condition_.notify_one();
	return true;
}

void SavedGame::close()
{
	if (file_handle_ != 0)
	{
		::FS_FCloseFile(file_handle_);
		file_handle_ = 0;
	}

	version_ = 0;

	clear_error();
	reset_buffer();

	saved_io_buffer_.clear();
	saved_io_buffer_offset_ = 0;

	rle_buffer_.clear();
	chunks_.clear();

	is_readable_ = false;
	is_writable_ = false;
}

void SavedGame::poll_write_results()
{
	std::deque<WriteResult> results;
	{
		std::lock_guard<std::mutex> lock(write_mutex_);
		results.swap(write_results_);
	}

	for (const WriteResult& result : results)
	{
		if (!result.error.empty())
		{
			::Com_Printf(
				S_COLOR_RED "Failed to write saved game \"%s\": %s\n",
				result.name.c_str(),
				result.error.c_str());
		}
	}
}

void SavedGame::wait_for_writes()
{
	{
		std::unique_lock<std::mutex> lock(write_mutex_);
		write_condition_.wait(
			lock,
			[this]() { return write_jobs_.empty() && !write_worker_busy_; });
	}
	poll_write_results();
}

int SavedGame::get_version() const
{
	return version_;
}

bool SavedGame::read_chunk(
	const uint32_t chunk_id)
{
	if (is_failed_)
	{
		return false;
	}

	if (file_handle_ == 0)
	{
		is_failed_ = true;
		error_message_ = "Not open or created.";
		return false;
	}

	io_buffer_offset_ = 0;

	const std::string chunk_id_string = get_chunk_id_string(
		chunk_id);

	::Com_DPrintf(
		"Attempting read of chunk %s\n",
		chunk_id_string.c_str());

	uint32_t loaded_chunk_id = 0;
	uint32_t loaded_data_size = 0;

	int loaded_chunk_size = ::FS_Read(
		&loaded_chunk_id,
		static_cast<int>(sizeof(loaded_chunk_id)),
		file_handle_);

	loaded_chunk_size += ::FS_Read(
		&loaded_data_size,
		static_cast<int>(sizeof(loaded_data_size)),
		file_handle_);

	const bool is_compressed = (static_cast<int32_t>(loaded_data_size) < 0);

	if (is_compressed)
	{
		loaded_data_size = -static_cast<int32_t>(loaded_data_size);
	}

	// Make sure we are loading the correct chunk...
	//
	if (loaded_chunk_id != chunk_id)
	{
		is_failed_ = true;

		const std::string loaded_chunk_id_string = get_chunk_id_string(
			loaded_chunk_id);

		error_message_ =
			"Loaded chunk ID (" +
				loaded_chunk_id_string +
				") does not match requested chunk ID (" +
				chunk_id_string +
				").";

		return false;
	}

	uint32_t loaded_checksum = 0;

#ifdef JK2_MODE
	// Get checksum...
	//
	loaded_chunk_size += ::FS_Read(
		&loaded_checksum,
		static_cast<int>(sizeof(loaded_checksum)),
		file_handle_);
#endif // JK2_MODE

	// Load in data and magic number...
	//
	uint32_t compressed_size = 0;

	if (is_compressed)
	{
		loaded_chunk_size += ::FS_Read(
			&compressed_size,
			static_cast<int>(sizeof(compressed_size)),
			file_handle_);

		rle_buffer_.resize(
			compressed_size);

		loaded_chunk_size += ::FS_Read(
			rle_buffer_.data(),
			compressed_size,
			file_handle_);

		io_buffer_.resize(
			loaded_data_size);

		decompress(
			rle_buffer_,
			io_buffer_);
	}
	else
	{
		io_buffer_.resize(
			loaded_data_size);

		loaded_chunk_size += ::FS_Read(
			io_buffer_.data(),
			loaded_data_size,
			file_handle_);
	}

#ifdef JK2_MODE
	uint32_t loaded_magic_value = 0;

	loaded_chunk_size += ::FS_Read(
		&loaded_magic_value,
		static_cast<int>(sizeof(loaded_magic_value)),
		file_handle_);

	if (loaded_magic_value != get_jo_magic_value())
	{
		is_failed_ = true;

		error_message_ =
			"Bad saved game magic for chunk " + chunk_id_string + ".";

		return false;
	}
#else
	// Get checksum...
	//
	loaded_chunk_size += ::FS_Read(
		&loaded_checksum,
		static_cast<int>(sizeof(loaded_checksum)),
		file_handle_);
#endif // JK2_MODE

	// Make sure the checksums match...
	//
	const uint32_t checksum = ::Com_BlockChecksum(
		io_buffer_.data(),
		static_cast<int>(io_buffer_.size()));

	if (loaded_checksum != checksum)
	{
		is_failed_ = true;

		error_message_ =
			"Failed checksum check for chunk " + chunk_id_string + ".";

		return false;
	}

	// Make sure we didn't encounter any read errors...
	std::size_t ref_chunk_size =
		sizeof(loaded_chunk_id) +
		sizeof(loaded_data_size) +
		sizeof(loaded_checksum) +
		(is_compressed ? sizeof(compressed_size) : 0) +
		(is_compressed ? compressed_size : io_buffer_.size());

#ifdef JK2_MODE
	ref_chunk_size += sizeof(loaded_magic_value);
#endif

	if (loaded_chunk_size != static_cast<int>(ref_chunk_size))
	{
		is_failed_ = true;

		error_message_ =
			"Error during loading chunk " + chunk_id_string + ".";

		return false;
	}

	return true;
}

bool SavedGame::is_all_data_read() const
{
	if (is_failed_)
	{
		return false;
	}

	if (file_handle_ == 0)
	{
		return false;
	}

	return io_buffer_.size() == io_buffer_offset_;
}

void SavedGame::ensure_all_data_read()
{
	if (!is_all_data_read())
	{
		error_message_ = "Not all expected data read.";

		throw_error();
	}
}

bool SavedGame::write_chunk(
	const uint32_t chunk_id)
{
	if (is_failed_)
	{
		return false;
	}

	if (!is_writable_)
	{
		is_failed_ = true;
		error_message_ = "Not open or created.";
		return false;
	}

	::Com_DPrintf(
		"Attempting write of chunk %s\n",
		get_chunk_id_string(chunk_id).c_str());

	Chunk chunk;
	chunk.id = chunk_id;
	chunk.data.swap(io_buffer_);
	chunks_.push_back(std::move(chunk));
	io_buffer_offset_ = 0;
	return true;
}

bool SavedGame::read(
	void* dst_data,
	int dst_size)
{
	if (is_failed_)
	{
		return false;
	}

	if (file_handle_ == 0)
	{
		is_failed_ = true;
		error_message_ = "Not open or created.";
		return false;
	}

	if (!dst_data)
	{
		is_failed_ = true;
		error_message_ = "Null pointer.";
		return false;
	}

	if (dst_size < 0)
	{
		is_failed_ = true;
		error_message_ = "Negative size.";
		return false;
	}

	if (!is_readable_)
	{
		is_failed_ = true;
		error_message_ = "Not readable.";
		return false;
	}

	if (dst_size == 0)
	{
		return true;
	}

	if ((io_buffer_offset_ + dst_size) > io_buffer_.size())
	{
		is_failed_ = true;
		error_message_ = "Not enough data.";
		return false;
	}

	std::uninitialized_copy_n(
		&io_buffer_[io_buffer_offset_],
		dst_size,
		static_cast<uint8_t*>(dst_data));

	io_buffer_offset_ += dst_size;

	return true;
}

bool SavedGame::write(
	const void* src_data,
	int src_size)
{
	if (is_failed_)
	{
		return false;
	}

	if (!is_writable_)
	{
		is_failed_ = true;
		error_message_ = "Not open or created.";
		return false;
	}

	if (!src_data)
	{
		is_failed_ = true;
		error_message_ = "Null pointer.";
		return false;
	}

	if (src_size < 0)
	{
		is_failed_ = true;
		error_message_ = "Negative size.";
		return false;
	}

	if (!is_writable_)
	{
		is_failed_ = true;
		error_message_ = "Not writable.";
		return false;
	}

	if (src_size == 0)
	{
		return true;
	}

	const std::size_t new_buffer_size = io_buffer_offset_ + src_size;

	io_buffer_.resize(
		new_buffer_size);

	std::uninitialized_copy_n(
		static_cast<const uint8_t*>(src_data),
		src_size,
		&io_buffer_[io_buffer_offset_]);

	io_buffer_offset_ = new_buffer_size;

	return true;
}

bool SavedGame::is_failed() const
{
	return is_failed_;
}

bool SavedGame::skip(
	int count)
{
	if (is_failed_)
	{
		return false;
	}

	if (!is_readable_ && !is_writable_)
	{
		is_failed_ = true;
		error_message_ = "Not open or created.";
		return false;
	}

	if (count < 0)
	{
		is_failed_ = true;
		error_message_ = "Negative count.";
		return false;
	}

	if (count == 0)
	{
		return true;
	}

	const std::size_t new_offset = io_buffer_offset_ + count;
	const std::size_t buffer_size = io_buffer_.size();

	if (new_offset > buffer_size)
	{
		if (is_readable_)
		{
			is_failed_ = true;
			error_message_ = "Not enough data.";
			return false;
		}
		else if (is_writable_)
		{
			if (new_offset > buffer_size)
			{
				io_buffer_.resize(
					new_offset);
			}
		}
	}

	io_buffer_offset_ = new_offset;

	return true;
}

void SavedGame::save_buffer()
{
	saved_io_buffer_ = io_buffer_;
	saved_io_buffer_offset_ = io_buffer_offset_;
}

void SavedGame::load_buffer()
{
	io_buffer_ = saved_io_buffer_;
	io_buffer_offset_ = saved_io_buffer_offset_;
}

const void* SavedGame::get_buffer_data() const
{
	return io_buffer_.data();
}

int SavedGame::get_buffer_size() const
{
	return static_cast<int>(io_buffer_.size());
}

void SavedGame::remove(
	const std::string& base_file_name)
{
	get_instance().wait_for_writes();

	const std::string path = generate_path(
		base_file_name);

	::FS_DeleteUserGenFile(
		path.c_str());
}

SavedGame& SavedGame::get_instance()
{
	static SavedGame result;
	return result;
}

void SavedGame::clear_error()
{
	is_failed_ = false;
	error_message_.clear();
}

void SavedGame::throw_error()
{
	if (error_message_.empty())
	{
		error_message_ = "Generic error.";
	}

	error_message_ = "SG: " + error_message_;

	::Com_Error(
		ERR_DROP,
		"%s",
		error_message_.c_str());
}

void SavedGame::compress(
	const Buffer& src_buffer,
	Buffer& dst_buffer)
{
	const int src_size = static_cast<int>(src_buffer.size());

	dst_buffer.resize(2 * src_size);

	int src_count = 0;
	int dst_index = 0;

	while (src_count < src_size)
	{
		int src_index = src_count;
		uint8_t b = src_buffer[src_index++];

		while (src_index < src_size &&
			(src_index - src_count) < 127 &&
			src_buffer[src_index] == b)
		{
			src_index += 1;
		}

		if ((src_index - src_count) == 1)
		{
			while (src_index < src_size &&
				(src_index - src_count) < 127 && (
					src_buffer[src_index] != src_buffer[src_index - 1] || (
						src_index > 1 &&
						src_buffer[src_index] != src_buffer[src_index - 2])))
			{
				src_index += 1;
			}

			while (src_index < src_size &&
				src_buffer[src_index] == src_buffer[src_index - 1])
			{
				src_index -= 1;
			}

			dst_buffer[dst_index++] =
				static_cast<uint8_t>(src_count - src_index);

			for (int i = src_count; i < src_index; ++i)
			{
				dst_buffer[dst_index++] = src_buffer[i];
			}
		}
		else
		{
			dst_buffer[dst_index++] =
				static_cast<uint8_t>(src_index - src_count);

			dst_buffer[dst_index++] = b;
		}

		src_count = src_index;
	}

	dst_buffer.resize(
		dst_index);
}

bool SavedGame::get_write_path(
	const std::string& base_file_name,
	std::string& path)
{
	char os_path[MAX_OSPATH];
	const std::string file_path = generate_path(base_file_name);
	if (!::FS_GetUserGenPath(file_path.c_str(), os_path, sizeof(os_path)))
	{
		error_message_ = "Failed to create the saved game path.";
		is_failed_ = true;
		return false;
	}

	path = os_path;
	return true;
}

void SavedGame::write_worker()
{
	for (;;)
	{
		WriteJob job;
		{
			std::unique_lock<std::mutex> lock(write_mutex_);
			write_condition_.wait(
				lock,
				[this]() { return stop_write_worker_ || !write_jobs_.empty(); });
			if (stop_write_worker_ && write_jobs_.empty())
			{
				return;
			}

			job = std::move(write_jobs_.front());
			write_jobs_.pop_front();
			write_worker_busy_ = true;
		}

		WriteResult result;
		result.name = job.name;
		write_job(job, result.error);

		{
			std::lock_guard<std::mutex> lock(write_mutex_);
			write_results_.push_back(std::move(result));
			write_worker_busy_ = false;
		}
		write_condition_.notify_all();
	}
}

bool SavedGame::write_job(
	const WriteJob& job,
	std::string& error)
{
	FILE* file = std::fopen(job.temporary_path.c_str(), "wb");
	if (!file)
	{
		error = std::strerror(errno);
		return false;
	}

	auto write_data = [file](const void* data, std::size_t size)
	{
		return std::fwrite(data, 1, size, file) == size;
	};

	Buffer compressed;
	for (const Chunk& chunk : job.chunks)
	{
		const uint32_t checksum = ::Com_BlockChecksum(
			chunk.data.data(),
			static_cast<int>(chunk.data.size()));
		const Buffer* data = &chunk.data;
		int32_t size = static_cast<int32_t>(chunk.data.size());

		if (job.compress)
		{
			compress(chunk.data, compressed);
			if (compressed.size() < chunk.data.size())
			{
				data = &compressed;
				size = -size;
			}
		}

		const uint32_t data_size = static_cast<uint32_t>(size);
		if (!write_data(&chunk.id, sizeof(chunk.id)) ||
			!write_data(&data_size, sizeof(data_size)))
		{
			error = "I/O error while writing a chunk header.";
			break;
		}

#ifdef JK2_MODE
		if (!write_data(&checksum, sizeof(checksum)))
		{
			error = "I/O error while writing a chunk checksum.";
			break;
		}
#endif

		if (size < 0)
		{
			const uint32_t compressed_size = static_cast<uint32_t>(data->size());
			if (!write_data(&compressed_size, sizeof(compressed_size)))
			{
				error = "I/O error while writing a compressed chunk size.";
				break;
			}
		}

		if (!write_data(data->data(), data->size()))
		{
			error = "I/O error while writing chunk data.";
			break;
		}

#ifdef JK2_MODE
		const uint32_t magic_value = get_jo_magic_value();
		if (!write_data(&magic_value, sizeof(magic_value)))
#else
		if (!write_data(&checksum, sizeof(checksum)))
#endif
		{
			error = "I/O error while finishing a chunk.";
			break;
		}
	}

	if (std::fclose(file) != 0 && error.empty())
	{
		error = std::strerror(errno);
	}
	if (!error.empty())
	{
		std::remove(job.temporary_path.c_str());
		return false;
	}

	if (!job.rotation_paths.empty())
	{
		const std::string& oldest = job.rotation_paths.back();
		if (std::remove(oldest.c_str()) != 0 && errno != ENOENT)
		{
			error = std::strerror(errno);
			return false;
		}

		for (std::size_t i = job.rotation_paths.size() - 1; i > 0; --i)
		{
			if (std::rename(job.rotation_paths[i - 1].c_str(), job.rotation_paths[i].c_str()) != 0 && errno != ENOENT)
			{
				error = std::strerror(errno);
				return false;
			}
		}
	}
	else if (std::remove(job.target_path.c_str()) != 0 && errno != ENOENT)
	{
		error = std::strerror(errno);
		return false;
	}

	if (std::rename(job.temporary_path.c_str(), job.target_path.c_str()) != 0)
	{
		error = std::strerror(errno);
		return false;
	}
	return true;
}

void SavedGame::decompress(
	const Buffer& src_buffer,
	Buffer& dst_buffer)
{
	int src_index = 0;
	int dst_index = 0;

	int remain_size = static_cast<int>(dst_buffer.size());

	while (remain_size > 0)
	{
		int8_t count = static_cast<int8_t>(src_buffer[src_index++]);

		if (count > 0)
		{
			std::uninitialized_fill_n(
				&dst_buffer[dst_index],
				count,
				src_buffer[src_index++]);
		}
		else
		{
			if (count < 0)
			{
				count = -count;

				std::uninitialized_copy_n(
					&src_buffer[src_index],
					count,
					&dst_buffer[dst_index]);

				src_index += count;
			}
		}

		dst_index += count;
		remain_size -= count;
	}
}

std::string SavedGame::generate_path(
	const std::string& base_file_name)
{
	std::string normalized_file_name = base_file_name;

	std::replace(
		normalized_file_name.begin(),
		normalized_file_name.end(),
		'/',
		'_');

	return "saves/" + normalized_file_name + ".sav";
}

std::string SavedGame::get_chunk_id_string(
	uint32_t chunk_id)
{
	std::string result(4, '\0');

	result[0] = static_cast<char>((chunk_id >> 24) & 0xFF);
	result[1] = static_cast<char>((chunk_id >> 16) & 0xFF);
	result[2] = static_cast<char>((chunk_id >> 8) & 0xFF);
	result[3] = static_cast<char>((chunk_id >> 0) & 0xFF);

	return result;
}

void SavedGame::reset_buffer()
{
	io_buffer_.clear();
	reset_buffer_offset();
}

void SavedGame::reset_buffer_offset()
{
	io_buffer_offset_ = 0;
}

const uint32_t SavedGame::get_jo_magic_value()
{
	return 0x1234ABCD;
}


} // ojk
