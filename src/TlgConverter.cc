#include <png.h>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
#include "BadMagicException.h"
#include "TlgConverter.h"
#include "AbstractTlgReader.h"
#include "Tlg0Reader.h"
#include "Tlg5Reader.h"
#include "Tlg6Reader.h"

const Image TlgConverter::read(std::string path) const
{
	std::ifstream ifs(path, std::ifstream::in | std::ifstream::binary);
	if (!ifs)
		throw std::runtime_error("Can\'t open " + path + " for reading");

	try
	{
		auto reader = AbstractTlgReader::choose_reader(ifs);
		auto ret = reader->read(path);
		ifs.close();
		return ret;
	}
	catch (std::exception const &e)
	{
		ifs.close();
		throw;
	}
}

void TlgConverter::save(const Image image, std::string path) const
{
	FILE *fp = fopen(path.c_str(), "wb");
	if (fp == nullptr)
		throw std::runtime_error("Could not open " + path + " for writing");

	png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	if (png_ptr == nullptr)
	{
		fclose(fp);
		throw std::runtime_error("Could not allocate write struct");
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == nullptr)
	{
		png_destroy_write_struct(&png_ptr, nullptr);
		fclose(fp);
		throw std::runtime_error("Could not allocate info struct");
	}

	if (setjmp(png_jmpbuf(png_ptr)))
	{
		png_destroy_write_struct(&png_ptr, &info_ptr);
		fclose(fp);
		throw std::runtime_error("Error during PNG creation");
	}

	png_init_io(png_ptr, fp);

	png_set_IHDR(
		png_ptr,
		info_ptr,
		image.width,
		image.height,
		8,
		PNG_COLOR_TYPE_RGBA,
		PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_DEFAULT,
		PNG_FILTER_TYPE_DEFAULT);

	png_set_compression_level(png_ptr, 6);
	png_write_info(png_ptr, info_ptr);

	const size_t row_size = static_cast<size_t>(image.width) * 4;
	std::vector<unsigned char> row_buffer(row_size);
	for (uint32_t y = 0; y < image.height; y ++)
	{
		memcpy(row_buffer.data(), reinterpret_cast<unsigned char*>(image.pixels) + y * row_size, row_size);
		png_write_row(png_ptr, row_buffer.data());
	}

	png_write_end(png_ptr, nullptr);
	png_destroy_write_struct(&png_ptr, &info_ptr);
	fclose(fp);
}
