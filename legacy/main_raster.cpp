#include <cstdio>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <windows.h>

//encoding utf-8


#define INF 1000000000.0f

const int DPI = 1000;
const int bitdpi = 10000;
const int MAXLENGTH = 200;

//support utf-8
char s[MAXLENGTH];
float vertexData[MAXLENGTH * DPI][4][2];
float Outvertex[MAXLENGTH * DPI][3];
float Invertex[MAXLENGTH * DPI][3];
FT_Error Error;
//AB
//CD like a "z"

// UTF-8解码函数
std::vector<uint32_t> decodeUTF8(const char *utf8_str) {
	std::vector<uint32_t> unicode_chars;
	const unsigned char *p = reinterpret_cast<const unsigned char *>(utf8_str);

	while (*p) {
		uint32_t ch = 0;
		int bytes = 0;

		if (*p < 0x80) {
			ch = *p;
			bytes = 1;
		} else if (*p < 0xE0) {
			ch = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
			bytes = 2;
		} else if (*p < 0xF0) {
			ch = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
			bytes = 3;
		} else {
			ch = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
			     ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
			bytes = 4;
		}

		bool valid = true;
		for (int i = 1; i < bytes; ++i) {
			if ((p[i] & 0xC0) != 0x80) {
				valid = false;
				break;
			}
		}

		if (valid) {
			unicode_chars.push_back(ch);
			p += bytes;
		} else {
			p++;
		}
	}

	return unicode_chars;
}


int FTM(char src[], float size, int dpi) {
	FT_Library library;
	FT_Face face;
	// 初始化freetype
	FT_Error error = FT_Init_FreeType(&library);
	if (error) {
		return -1;
	}
	// 尝试加载字体文件
	error = FT_New_Face(library, src, 0, &face);
	if (error) {
		Error = error;
		FT_Done_FreeType(library);
		return -1;
	}

	FT_Set_Pixel_Sizes(face, 0, bitdpi);

	std::vector<uint32_t> unicode_chars = decodeUTF8(s);

	int facen = 0;
	int yfacen = 0;

	float penX = 1.0f;
	for (uint32_t char_code : unicode_chars) {
		if (FT_Load_Char(face, char_code, FT_LOAD_RENDER | FT_LOAD_NO_HINTING | FT_LOAD_MONOCHROME)) {
			if (FT_Load_Char(face, 0xFFFD, FT_LOAD_RENDER | FT_LOAD_NO_HINTING | FT_LOAD_MONOCHROME)) {
				continue;
			}
		}
		FT_GlyphSlot glyph = face->glyph;
		FT_Bitmap bitmap = glyph->bitmap;
		if (bitmap.buffer == NULL) {
			Error = error;
			FT_Done_Face(face);
			FT_Done_FreeType(library);
			return -1;
		}
		int left = glyph->bitmap_left;      // 位图左侧到原点的水平距离
		int top = glyph->bitmap_top;        // 位图顶部到基线的垂直距离
		int outn = 0;
		int inn = 0;
		int dw1 = bitdpi / dpi;
		for (int i = 0; i < bitmap.rows; i += dw1) {
			bool started = false;
			for (int j = 0; j < bitmap.width; j++) {
				// 计算字节索引和位索引(单色位图, MSB 优先)
				int byte_index = j / 8;
				int bit_index = 7 - (j % 8); // MSB 对应最左像素
				unsigned char byte = bitmap.buffer[i * bitmap.pitch + byte_index];

				// 检查像素是否设置（非零）
				if (byte & (1 << bit_index)) {
					// 填充部分
					if (!started) {
						started = true;
						vertexData[facen][0][0] = (penX + j + left) / (float)bitdpi * size;
						vertexData[facen][0][1] = (top - i) / (float)bitdpi * size;
						vertexData[facen][2][0] = (penX + j + left) / (float)bitdpi * size;
						vertexData[facen][2][1] = (top - i - dw1) / (float)bitdpi * size;
						//判断是否为“外”点
						bool upo = false;
						if (i == 0) {
							upo = true;
						} else {
							unsigned char byteU = bitmap.buffer[(i-dw1) * bitmap.pitch + byte_index];
							if (!(byteU & (1 << bit_index))) {
								upo = true;
							}
						}
						bool dwo = false;
						if (i >= bitmap.rows - dw1) {
							dwo = true;
						} else {
							unsigned char byteD = bitmap.buffer[(i+dw1) * bitmap.pitch + byte_index];
							if (!(byteD & (1 << bit_index))) {
								dwo = true;
							}
						}
						if (upo) {
							Outvertex[outn][0] = (penX + j + left) / (float)bitdpi * size;
							Outvertex[outn][1] = (top - i) / (float)bitdpi * size;
							Outvertex[outn][2] = 1;//A
							outn++;
						}
						if (dwo) {
							Outvertex[outn][0] = (penX + j + left) / (float)bitdpi * size;
							Outvertex[outn][1] = (top - i - dw1) / (float)bitdpi * size;
							Outvertex[outn][2] = 3;//C
							outn++;
						}
						//判断是否为“内“点
						int byte_index = (j - 1) / 8;
						int bit_index = 7 - ((j - 1) % 8);
						bool upi = false;
						if (j >= 1 and i >= dw1) {
							unsigned char byteU = bitmap.buffer[(i-dw1) * bitmap.pitch + byte_index];
							if (byteU & (1 << bit_index)) {
								upi = true;
							}
						}
						bool dwi = false;
						if (j >= 1 and i <  bitmap.rows - dw1) {
							unsigned char byteD = bitmap.buffer[(i+dw1) * bitmap.pitch + byte_index];
							if (byteD & (1 << bit_index)) {
								dwi = true;
							}
						}
						if (upi) {
							Invertex[inn][0] = (penX + j + left) / (float)bitdpi * size;
							Invertex[inn][1] = (top - i) / (float)bitdpi * size;
							Invertex[inn][2] = 1;//A
							inn++;
						}
						if (dwi) {
							Invertex[inn][0] = (penX + j + left) / (float)bitdpi * size;
							Invertex[inn][1] = (top - i - dw1) / (float)bitdpi * size;
							Invertex[inn][2] = 3;//C
							inn++;
						}
					}
				} else {
					// 空白部分
					if (started) {
						started = false;
						vertexData[facen][1][0] = (penX + j + left) / (float)bitdpi * size;
						vertexData[facen][1][1] = (top - i) / (float)bitdpi * size;
						vertexData[facen][3][0] = (penX + j + left) / (float)bitdpi * size;
						vertexData[facen][3][1] = (top - i - dw1) / (float)bitdpi * size;
						for (int k = yfacen; k < facen; k++) { //合并相同面
							if (vertexData[k][3][0] == vertexData[facen][1][0] and vertexData[k][2][0] == vertexData[facen][0][0]
							        and vertexData[k][3][1] == vertexData[facen][1][1] and vertexData[k][2][1] == vertexData[facen][0][1]) {
								vertexData[k][2][0] = vertexData[facen][2][0];
								vertexData[k][3][0] = vertexData[facen][3][0];
								vertexData[k][2][1] = vertexData[facen][2][1];
								vertexData[k][3][1] = vertexData[facen][3][1];
								facen--;
								break;
							}
						}
						facen++;
						if (facen >= MAXLENGTH * DPI - 1 ) {
							return facen;
						}
						int byte_index = (j - 1) / 8;
						int bit_index = 7 - ((j - 1) % 8); 

						//判断是否为“外”点
						bool upo = false;
						if (i <= dw1) {
							upo = true;
						} else {
							unsigned char byteU = bitmap.buffer[(i-dw1) * bitmap.pitch + byte_index];
							if (!(byteU & (1 << bit_index))) {
								upo = true;
							}
						}
						bool dwo = false;
						if (i >= bitmap.rows - dw1) {
							dwo = true;
						} else {
							unsigned char byteD = bitmap.buffer[(i+dw1) * bitmap.pitch + byte_index];
							if (!(byteD & (1 << bit_index))) {
								dwo = true;
							}
						}
						if (upo) {
							Outvertex[outn][0] = (penX + j + left) / (float)bitdpi * size;
							Outvertex[outn][1] = (top - i) / (float)bitdpi * size;
							Outvertex[inn][2] = 2;//B
							outn++;
						}
						if (dwo) {
							Outvertex[outn][0] = (penX + j + left) / (float)bitdpi * size;
							Outvertex[outn][1] = (top - i - dw1) / (float)bitdpi * size;
							Outvertex[inn][2] = 4;//D
							outn++;
						}
						//判断是否为“内“点
						byte_index = j / 8;
						bit_index = 7 - (j % 8);
						bool upi = false;
						if (i > dw1) {
							unsigned char byteU = bitmap.buffer[(i-dw1) * bitmap.pitch + byte_index];
							if (byteU & (1 << bit_index)) {
								upi = true;
							}
						}
						bool dwi = false;
						if (i < bitmap.rows - dw1) {
							unsigned char byteD = bitmap.buffer[(i+dw1) * bitmap.pitch + byte_index];
							if (byteD & (1 << bit_index)) {
								dwi = true;
							}
						}
						if (upi) {
							Invertex[inn][0] = (penX + j + left) / (float)bitdpi * size;
							Invertex[inn][1] = (top - i) / (float)bitdpi * size;
							Invertex[inn][2] = 2;//B
							inn++;
						}
						if (dwi) {
							Invertex[inn][0] = (penX + j + left) / (float)bitdpi * size;
							Invertex[inn][1] = (top - i - dw1) / (float)bitdpi * size;
							Invertex[inn][2] = 4;//D
							inn++;
						}
					}
				}
			}
			//强制结束
			if (started) {
				vertexData[facen][1][0] = (penX + bitmap.width + left) / (float)bitdpi * size;
				vertexData[facen][1][1] = (top - i) / (float)bitdpi * size;
				vertexData[facen][3][0] = (penX + bitmap.width + left) / (float)bitdpi * size;
				vertexData[facen][3][1] = (top - i - dw1) / (float)bitdpi * size;
				for (int k = yfacen; k < facen; k++) { //合并相同面
					if (vertexData[k][3][0] == vertexData[facen][1][0] and vertexData[k][2][0] == vertexData[facen][0][0]
					        and vertexData[k][3][1] == vertexData[facen][1][1] and vertexData[k][2][1] == vertexData[facen][0][1]) {
						vertexData[k][2][0] = vertexData[facen][2][0];
						vertexData[k][3][0] = vertexData[facen][3][0];
						vertexData[k][2][1] = vertexData[facen][2][1];
						vertexData[k][3][1] = vertexData[facen][3][1];
						facen--;
						break;
					}
				}
				facen++;
				if (facen >= MAXLENGTH * DPI - 1)
					return facen;

				int byte_index = (bitmap.width - 1) / 8;
				int bit_index = 7 - ((bitmap.width - 1) % 8);
				//判断是否为“点”
				bool upo = false;
				if (i <= dw1) {
					upo = true;
				} else {
					unsigned char byteU = bitmap.buffer[(i-dw1) * bitmap.pitch + byte_index];
					if (!(byteU & (1 << bit_index))) {
						upo = true;
					}
				}
				bool dwo = false;
				if (i >= bitmap.rows - dw1) {
					dwo = true;
				} else {
					unsigned char byteD = bitmap.buffer[(i+dw1) * bitmap.pitch + byte_index];
					if (!(byteD & (1 << bit_index))) {
						dwo = true;
					}
				}
				if (upo) {
					Outvertex[outn][0] = (penX + bitmap.width + left) / (float)bitdpi * size;
					Outvertex[outn][1] = (top - i) / (float)bitdpi * size;
					outn++;
				}
				if (dwo) {
					Outvertex[outn][0] = (penX + bitmap.width + left) / (float)bitdpi * size;
					Outvertex[outn][1] = (top - i - dw1) / (float)bitdpi * size;
					outn++;
				}
				//不可能为内点
			}
		}
		for (int i = 0; i < inn; i++) {
			float x1 = Invertex[i][0];
			float y1 = Invertex[i][1];
			int type = (int)(Invertex[i][2] + 0.5);

			//每个内点
			//最近的y点
			float y2 = INF;
			for (int j = 0; j < outn; j++) {
				if ((type - 2.5) * (Outvertex[j][1] - y1) > 0) {
					if (abs(x1 - Outvertex[j][0]) < 0.001f and abs(y1 - Outvertex[j][1]) < abs(y1 - y2)) {
						y2 = Outvertex[j][1];
					}
				}
			}
			for (int j = 0; j < inn; j++) {
				if ((type - 2.5) * (Invertex[j][1] - y1) > 0) {
					if (abs(x1 - Invertex[j][0]) < 0.001f and abs(y1 - Invertex[j][1]) < abs(y1 - y2)
					        and abs(y1 - Invertex[j][1]) > 0.001f) {
						y2 = Invertex[j][1];
					}
				}
			}
			//最近的x点
			float x2 = INF;
			for (int j = 0; j < outn; j++) {
				if ((type % 2 - 0.5) * (x1 - Outvertex[j][0]) > 0) {
					if (abs(y1 - Outvertex[j][1]) < 0.001f and abs(x1 - Outvertex[j][0]) < abs(x1 - x2)) {
						x2 = Outvertex[j][0];
					}
				}
			}
			for (int j = 0; j < inn; j++) {
				if ((type % 2 - 0.5) * (x1 - Invertex[j][0]) > 0) {
					if (abs(y1 - Invertex[j][1]) < 0.001f and abs(x1 - Invertex[j][0]) < abs(x1 - x2)
					        and abs(x1 - Invertex[j][0]) > 0.001f) {
						x2 = Invertex[j][0];
					}
				}
			}
			if (y2 != INF && x2 != INF && abs(x2 - x1) < bitmap.width / (float)bitdpi * size * 0.5
			        && abs(y2 - y1) < bitmap.width / (float)bitdpi * size * 0.5) {
				if (abs((x2 - x1) * (y2 - y1)) < bitmap.width * bitmap.rows / (float)bitdpi * size / (float)bitdpi * size * 0.01) {
					if ((x2 - x1) * (y2 - y1) < 0) {
						vertexData[facen][0][0] = x1;
						vertexData[facen][0][1] = y1;
						vertexData[facen][1][0] = x1;
						vertexData[facen][1][1] = y1;
						vertexData[facen][2][0] = x1;
						vertexData[facen][2][1] = y2;
						vertexData[facen][3][0] = x2;
						vertexData[facen][3][1] = y1;
						if (abs(1 / (float)dpi * size - abs(y2 - y1)) < 0.001f) {
							for (int k = yfacen; k < facen; k++) { //合并相同面
								if (abs(vertexData[k][0][0] - x1) < size/(float)bitdpi and abs(vertexData[k][2][0] - x1) < size/(float)bitdpi and
								    abs(vertexData[k][0][1] - y2) < size/(float)bitdpi and abs(vertexData[k][2][1] - y1) < size/(float)bitdpi and type == 3) {
									vertexData[k][2][0] = x2;
									facen--;
									break;
								}
								if (abs(vertexData[k][1][0] - x1) < size/(float)bitdpi and abs(vertexData[k][3][0] - x1) < size/(float)bitdpi and
								    abs(vertexData[k][1][1] - y1) < size/(float)bitdpi and abs(vertexData[k][3][1] - y2) < size/(float)bitdpi and type == 2) {
									vertexData[k][1][0] = x2;
									facen--;
									break;
								}
							}
						}
					} else {
						vertexData[facen][0][0] = x1;
						vertexData[facen][0][1] = y2;
						vertexData[facen][1][0] = x1;
						vertexData[facen][1][1] = y2;
						vertexData[facen][2][0] = x1;
						vertexData[facen][2][1] = y1;
						vertexData[facen][3][0] = x2;
						vertexData[facen][3][1] = y1;
						if (abs(1 / (float)dpi * size - abs(y2 - y1)) < 0.001f) {
							for (int k = yfacen; k < facen; k++) { //合并相同面
								if (abs(vertexData[k][0][0] - x1) < size/(float)bitdpi and abs(vertexData[k][2][0] - x1) < size/(float)bitdpi and
								    abs(vertexData[k][0][1] - y1) < size/(float)bitdpi and abs(vertexData[k][2][1] - y2) < size/(float)bitdpi and type == 1) {
									vertexData[k][0][0] = x2;
									facen--;
									break;
								}
								if (abs(vertexData[k][1][0] - x1) < size/(float)bitdpi and abs(vertexData[k][3][0] - x1) < size/(float)bitdpi and
								    abs(vertexData[k][1][1] - y2) < size/(float)bitdpi and abs(vertexData[k][3][1] - y1) < size/(float)bitdpi and type == 4) {
									vertexData[k][3][0] = x2;
									facen--;
									break;
								}
							}
						}
					}
					facen++;
					if (facen >= MAXLENGTH * DPI - 1) {
						return facen;
					}
				}
			}
		}
		yfacen = facen;
		penX += glyph->advance.x / 64.0f;
	}



	FT_Done_Face(face);
	FT_Done_FreeType(library);
	return facen;
}


int main() {
	SetConsoleOutputCP(65001); // 设置控制台输出编码
	SetConsoleCP(65001);       // 设置控制台输入编码
	while (1) {
		int dpi;
		float size;
		char name[256] = {0};
		printf("请输入字体名称(带后缀)：");
		scanf("%s", name);
		printf("请输入精度(20~1000)");
		       scanf("%d", &dpi);
		if (dpi > 1000) {
		dpi = 1000;
	} else if (dpi < 20) {
		dpi = 20;
	}
	printf("请输入大小：");
	scanf("%f", &size);
	printf("请输入文本(Windows换行再按ctrl+z后换行结束输入，不支持换行符制表符等符号):\n");
	{
		int i;
		for (i = 0; scanf("%c", s + i) != EOF and i < MAXLENGTH - 1; i++)
				if (s[i] == '\n')
					i--;
			if (s[i] == '\n') {
				s[i] = '\0';
			}
		}
		int ends;

		char src[1024] = {0};
		                 sprintf(src, "fonts/%s", name);
		                 ends = FTM(src, size, dpi);

		if (ends == -1 or ends == 0) {
		const char *error_string = FT_Error_String(Error);
			printf("\n生成出错(代码%d):%s\n", Error, error_string);

			return (int)Error;
		}
		printf("\n生成完成:总计 %d 个面。\n", ends);

		if (ends >= MAXLENGTH * DPI - 1) {
		printf("错误:面过多，请降低精度。\n");
			return 1;
		}
		printf("写入output.na: %s\n", s);

		FILE *f = freopen("output.na", "w", stdout);

		          //头部
		          printf("<root>\n  <ship author=\"Duang's generator\" description=\"%s By Duang's generator.\" alwaysgeneratenewthumbnail=\"True\" hornType=\"1\" hornPitch=\"1\" tracerCol=\"E53D4FFF\">\n"
		                 , s);

		for (int i = 0; i < ends; i++) {
		printf("    <part id=\"701\" instanceId=\"%d\">\n", i);

			printf("      <vertexData ax=\"%f\" ay=\"%f\" az=\"%f\" bx=\"%f\" by=\"%f\" bz=\"%f\" cx=\"%f\" cy=\"%f\" cz=\"%f\" dx=\"%f\" dy=\"%f\" dz=\"%f\"",
			       0.1, vertexData[i][0][1], vertexData[i][0][0], 0.1, vertexData[i][1][1], vertexData[i][1][0], 0.1,
			       vertexData[i][2][1], vertexData[i][2][0], 0.1, vertexData[i][3][1], vertexData[i][3][0]);
			printf(" ix=\"%f\" iy=\"%f\" iz=\"%f\" iix=\"%f\" iiy=\"%f\" iiz=\"%f\" iiix=\"%f\" iiiy=\"%f\" iiiz=\"%f\" ivx=\"%f\" ivy=\"%f\" ivz=\"%f\" sym=\"None\" />\n",
			       -0.1, vertexData[i][0][1], vertexData[i][0][0], -0.1, vertexData[i][1][1], vertexData[i][1][0], -0.1,
			       vertexData[i][2][1], vertexData[i][2][0], -0.1, vertexData[i][3][1], vertexData[i][3][0]);

			printf("      <position x=\"0\" y=\"0\" z=\"0\" />\n");
			printf("      <rotation x=\"0\" y=\"0\" z=\"0\" />\n");
			printf("      <scale x=\"1\" y=\"1\" z=\"1\" />\n");
			printf("      <color hex=\"6F7275\" />\n");
			printf("      <armor value=\"5\" />\n");
			printf("    </part>\n");
		}
		printf("  </ship>\n</root>");
		fclose(f);
		if (freopen("CON", "w", stdout) == nullptr) {
		printf("Failed to reopen CON");
			return 1;
		}
		printf("按下ctrl+C以结束程序 ...\n");
	}


	return 0;
}