#include <QPainter>

#include "entity/HLMVStudioModelEntity.hpp"
#include "ui/assets/studiomodel/StudioModelTextureUtilities.hpp"

namespace ui
{
std::optional<std::tuple<studiomdl::TextureData, bool>> ConvertImageToTexture(QImage image)
{
	const QImage::Format inputFormat = image.format();

	const bool convertToIndexed8 = inputFormat != QImage::Format::Format_Indexed8;

	if (convertToIndexed8)
	{
		image.convertTo(QImage::Format::Format_Indexed8);
	}

	const QVector<QRgb> palette = image.colorTable();

	if (palette.isEmpty())
	{
		return {};
	}

	//Convert to 8 bit palette based image
	std::vector<std::byte> pixels;

	pixels.resize(image.width() * image.height());

	{
		std::byte* dest = pixels.data();

		for (int y = 0; y < image.height(); ++y)
		{
			for (int x = 0; x < image.width(); ++x, ++dest)
			{
				*dest = std::byte(image.pixelIndex(x, y));
			}
		}
	}

	graphics::RGBPalette convertedPalette;

	int paletteIndex;

	for (paletteIndex = 0; paletteIndex < palette.size(); ++paletteIndex)
	{
		const auto rgb = palette[paletteIndex];

		convertedPalette[paletteIndex] =
		{
			static_cast<std::uint8_t>(qRed(rgb)),
			static_cast<std::uint8_t>(qGreen(rgb)),
			static_cast<std::uint8_t>(qBlue(rgb))
		};
	}

	//Fill remaining entries with black
	for (; paletteIndex < convertedPalette.EntriesCount; ++paletteIndex)
	{
		convertedPalette[paletteIndex] = {0, 0, 0};
	}

	return std::tuple{studiomdl::TextureData{image.width(), image.height(), std::move(pixels), convertedPalette}, convertToIndexed8};
}

QImage ConvertTextureToRGBImage(
	const studiomdl::TextureData& texture, const std::byte* textureData, const graphics::RGBPalette& texturePalette, std::vector<QRgb>& dataBuffer)
{
	dataBuffer.resize(texture.Width * texture.Height);

	for (int y = 0; y < texture.Height; ++y)
	{
		for (int x = 0; x < texture.Width; ++x)
		{
			const auto& color = texturePalette[std::to_integer<int>(textureData[(texture.Width * y) + x])];

			dataBuffer[(texture.Width * y) + x] = qRgb(color.R, color.G, color.B);
		}
	}

	return QImage{reinterpret_cast<const uchar*>(dataBuffer.data()), texture.Width, texture.Height, QImage::Format::Format_RGB32};
}

QImage ConvertTextureToIndexed8Image(const studiomdl::TextureData& texture)
{
	//Ensure data is 32 bit aligned
	const int alignedWidth = (texture.Width + 3) & (~3);

	std::vector<uchar> alignedPixels;

	alignedPixels.resize(alignedWidth * texture.Height);

	for (int h = 0; h < texture.Height; ++h)
	{
		for (int w = 0; w < texture.Width; ++w)
		{
			alignedPixels[(alignedWidth * h) + w] = std::to_integer<uchar>(texture.Pixels[(texture.Width * h) + w]);
		}
	}

	QImage textureImage{alignedPixels.data(), texture.Width, texture.Height, QImage::Format::Format_Indexed8};

	QVector<QRgb> palette;

	palette.reserve(texture.Palette.GetSizeInBytes());

	for (const auto& rgb : texture.Palette)
	{
		palette.append(qRgb(rgb.R, rgb.G, rgb.B));
	}

	textureImage.setColorTable(palette);

	return textureImage;
}

QImage CreateUVMapImage(
	const studiomdl::EditableStudioModel& model, int textureIndex, int meshIndex, bool antiAliasLines, float textureScale, qreal lineWidth)
{
	const auto& texture = model.Textures[textureIndex]->Data;

	//RGBA format because only the UV lines need to be drawn, with no background
	QImage image{static_cast<int>(std::ceil(texture.Width * textureScale)), static_cast<int>(std::ceil(texture.Height * textureScale)),
		QImage::Format::Format_RGBA8888};

	//Set as transparent
	image.fill(Qt::transparent);

	QPainter painter{&image};

	painter.setPen(QPen{Qt::white, lineWidth});
	painter.setRenderHint(QPainter::RenderHint::Antialiasing, antiAliasLines);

	auto fixCoords = [=](int x, int y)
	{
		return QPointF(x * textureScale, y * textureScale);
	};

	auto meshes = model.ComputeMeshList(textureIndex);

	if (meshIndex != -1)
	{
		auto singleMesh = meshes[meshIndex];
		meshes.clear();
		meshes.emplace_back(singleMesh);
	}

	for (const auto mesh : meshes)
	{
		auto ptricmds = mesh->Triangles.data();

		for (int i; i = *(ptricmds++);)
		{
			if (i < 0)
			{
				i = -i;

				const auto firstVertex{fixCoords(ptricmds[2], ptricmds[3])};

				ptricmds += 4;
				--i;

				for (; i > 0; --i, ptricmds += 4)
				{
					painter.drawLine(firstVertex, fixCoords(ptricmds[2], ptricmds[3]));

					if (i > 1)
					{
						painter.drawLine(fixCoords(ptricmds[2], ptricmds[3]), fixCoords(ptricmds[6], ptricmds[7]));
					}
				}
			}
			else
			{
				auto firstVertex{fixCoords(ptricmds[2], ptricmds[3])};
				auto secondVertex{fixCoords(ptricmds[6], ptricmds[7])};

				painter.drawLine(firstVertex, secondVertex);

				ptricmds += 8;
				i -= 2;

				for (; i > 0; --i, ptricmds += 4)
				{
					painter.drawLine(secondVertex, fixCoords(ptricmds[2], ptricmds[3]));
					painter.drawLine(fixCoords(ptricmds[2], ptricmds[3]), firstVertex);

					firstVertex = secondVertex;
					secondVertex = fixCoords(ptricmds[2], ptricmds[3]);
				}
			}
		}
	}

	return image;
}

void DrawUVImage(const QColor& backgroundColor, bool showUVMap, bool overlayOnTexture, const QImage& texture, const QImage& uvMap, QImage& target)
{
	target.fill(backgroundColor);

	QPainter painter{&target};

	const QRect drawRect{0, 0, target.width(), target.height()};

	if (!showUVMap || overlayOnTexture)
	{
		painter.drawImage(drawRect, texture);
	}

	if (showUVMap)
	{
		painter.drawImage(drawRect, uvMap);
	}
}
}
