// foo_input_ofr.cpp contains an OptimFROG Lossless/DualStream audio
// input plug-in for foobar2000 playback with cue sheet support

// Copyright (C) 2005-2011 Florin Ghido

#include <SDK/foobar2000.h>
#include <helpers/cue_parser.h>
#include "../OptimFROG/OptimFROG.h"

namespace
{
	DECLARE_COMPONENT_VERSION(
		"OptimFROG Lossless/DualStream Decoder",
		"1.44",
		"foobar2000 OptimFROG input plug-in\n"
		"OptimFROG Lossless/DualStream audio DLL\n"
		"Copyright (C) 1996-2011 Florin Ghido, all rights reserved.\n"
		"Visit http://www.LosslessAudio.org for updates\n"
		"Free for non-commercial use. E-mail: FlorinGhido@yahoo.com"
	);

	VALIDATE_COMPONENT_FILENAME("foo_input_ofr.dll")
	DECLARE_FILE_TYPE("OptimFROG files", "*.ofr;*.ofs");

	struct foobar2000_reader_t
	{
		file_ptr file;
	};

	#define GET_FILE(instance) (((foobar2000_reader_t*) instance)->file)

	condition_t f_foobar2000_close(void*)
	{
		return C_TRUE;
	}

	sInt32_t f_foobar2000_read(void* instance, void* destBuffer, uInt32_t count)
	{
		return static_cast<sInt32_t>(GET_FILE(instance)->read(destBuffer, count, fb2k::noAbort));
	}

	condition_t f_foobar2000_eof(void*)
	{
		return C_FALSE;
	}

	condition_t f_foobar2000_seekable(void* instance)
	{
		return GET_FILE(instance)->can_seek();
	}

	sInt64_t f_foobar2000_length(void* instance)
	{
		return GET_FILE(instance)->get_size(fb2k::noAbort);
	}

	sInt64_t f_foobar2000_getPos(void* instance)
	{
		return GET_FILE(instance)->get_position(fb2k::noAbort);
	}

	condition_t f_foobar2000_seek(void* instance, sInt64_t pos)
	{
		GET_FILE(instance)->seek(pos, fb2k::noAbort);
		return C_TRUE;
	}

	static ReadInterface rInt_foobar2000 =
	{
		f_foobar2000_close,
		f_foobar2000_read,
		f_foobar2000_eof,
		f_foobar2000_seekable,
		f_foobar2000_length,
		f_foobar2000_getPos,
		f_foobar2000_seek
	};

	class InputOFR : public input_stubs
	{
	public:
		InputOFR() : m_instance(OptimFROG_createInstance()) {}

		~InputOFR()
		{
			if (m_instance != C_NULL)
			{
				OptimFROG_destroyInstance(m_instance);
				m_instance = C_NULL;
			}
		}

		static GUID g_get_guid()
		{
			static constexpr GUID g = { 0x47bf9957, 0xdbc, 0x4fe6, { 0xb2, 0x56, 0x8d, 0xe5, 0x26, 0x46, 0xbc, 0x9e } };
			return g;
		}

		static bool g_is_our_content_type(const char*)
		{
			return false;
		}

		static bool g_is_our_path(const char*, const char* extension)
		{
			return stricmp_utf8(extension, "ofr") == 0 || stricmp_utf8(extension, "ofs") == 0;
		}

		static const char* g_get_name()
		{
			return "OptimFROG Decoder";
		}

		bool decode_can_seek()
		{
			return OptimFROG_seekable(m_instance) == C_TRUE;
		}

		bool decode_run(audio_chunk& chunk, abort_callback&)
		{
			const auto pointsRetrieved = OptimFROG_read(m_instance, m_buffer.get_ptr(), DELTA_POINTS);
			if (pointsRetrieved <= 0)
			{
				return false;
			}

			const uint32_t bytes = pointsRetrieved * m_info.channels * m_info.bitspersample / 8;
			chunk.set_data_fixedpoint(
				m_buffer.get_ptr(),
				bytes,
				m_info.samplerate,
				m_info.channels,
				m_info.bitspersample,
				audio_chunk::g_guess_channel_config(m_info.channels));

			return true;
		}

		t_filestats get_file_stats(abort_callback& abort)
		{
			return m_reader.file->get_stats(abort);
		}

		t_filestats2 get_stats2(uint32_t flags, abort_callback& abort)
		{
			return m_reader.file->get_stats2_(flags, abort);
		}

		void decode_initialize(uint32_t, abort_callback& abort)
		{
			if (OptimFROG_getPos(m_instance) != 0)
			{
				OptimFROG_close(m_instance);
				m_reader.file->reopen(abort);
			}
		}

		void decode_seek(double seconds, abort_callback&)
		{
			m_reader.file->ensure_seekable();

			if (!OptimFROG_seekPoint(m_instance, fb2k_audio_math::time_to_samples(seconds, m_info.samplerate)))
			{
				throw exception_io_data();
			}
		}

		void get_info(file_info& info, abort_callback& abort)
		{
			get_meta(info, abort);

			info.info_set("mode", m_info.method);
			info.info_set("speedup", m_info.speedup);
			info.info_set_bitrate(m_info.bitrate);
			info.info_set_int("bitspersample", m_info.bitspersample);
			info.info_set_int("channels", m_info.channels);
			info.info_set_int("samplerate", m_info.samplerate);
			info.info_set_int("version", m_info.version);
			info.set_length(fb2k_audio_math::samples_to_time(m_info.noPoints, m_info.samplerate));

			if (m_ofs)
			{
				info.info_set("encoding", "hybrid");
				info.info_set("codec", "DualStream");
			}
			else
			{
				info.info_set("encoding", "lossless");
				info.info_set("codec", "OptimFROG");
			}
		}

		void open(file_ptr filehint, const char* path, t_input_open_reason reason, abort_callback& abort)
		{
			m_ofs = pfc::string_extension(path).toLower() == "ofs";
			m_reader.file = filehint;
			input_open_file_helper(m_reader.file, path, reason, abort);

			if (OptimFROG_openExt(m_instance, &rInt_foobar2000, &m_reader, C_FALSE))
			{
				OptimFROG_getInfo(m_instance, &m_info);
				const uint32_t size = DELTA_POINTS * m_info.channels * m_info.bitspersample / 8;
				m_buffer.set_size(size);
			}
			else
			{
				OptimFROG_destroyInstance(m_instance);
				m_instance = C_NULL;
				throw exception_io_data();
			}
		}

		void remove_tags(abort_callback& abort)
		{
			m_reader.file->ensure_seekable();
			tag_processor::remove_trailing(m_reader.file, abort);
		}

		void retag(const file_info& info, abort_callback& abort)
		{
			m_reader.file->ensure_seekable();
			tag_processor::write_apev2(m_reader.file, info, abort);
		}

	private:
		void get_meta(file_info& info, abort_callback& abort)
		{
			if (!m_reader.file->can_seek()) return;

			const auto pos = m_reader.file->get_position(abort);
			try { tag_processor::read_trailing(m_reader.file, info, abort); }
			catch (...) {}
			m_reader.file->seek(pos, abort);
		}

		static constexpr int DELTA_POINTS = 1024;

		OptimFROG_Info m_info{};
		bool m_ofs{};
		foobar2000_reader_t m_reader;
		pfc::array_t<uint8_t> m_buffer;
		void* m_instance;
	};

	static input_cuesheet_factory_t<InputOFR> g_input_ofr_factory;

	class AlbumArtEditor : public album_art_editor_v2
	{
	public:
		GUID get_guid() final
		{
			return InputOFR::g_get_guid();
		}

		album_art_editor_instance_ptr open(file_ptr filehint, const char* path, abort_callback& abort) final
		{
			file_ptr file(filehint);
			if (file.is_empty()) filesystem::g_open(file, path, filesystem::open_mode_write_existing, abort);
			return tag_processor_album_art_utils::get()->edit(file, abort);
		}

		bool is_our_path(const char* path, const char* extension) final
		{
			return InputOFR::g_is_our_path(path, extension);
		}
	};

	class AlbumArtExtractor : public album_art_extractor_v2
	{
	public:
		GUID get_guid() final
		{
			return InputOFR::g_get_guid();
		}

		album_art_extractor_instance_ptr open(file_ptr filehint, const char* path, abort_callback& abort) final
		{
			file_ptr file(filehint);
			if (file.is_empty()) filesystem::g_open_read(file, path, abort);
			return tag_processor_album_art_utils::get()->open(file, abort);
		}

		bool is_our_path(const char* path, const char* extension) final
		{
			return InputOFR::g_is_our_path(path, extension);
		}
	};

	FB2K_SERVICE_FACTORY(AlbumArtEditor);
	FB2K_SERVICE_FACTORY(AlbumArtExtractor);
}
