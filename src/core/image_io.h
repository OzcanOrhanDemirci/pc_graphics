// PNG / EXR writing and PNG reading (stb, tinyexr).
#pragma once
#include <string>
#include <vector>

bool writePNG(const std::string& path, int w, int h, const unsigned char* rgba);
void setPNGCompression(int level);   // zlib level for the PNGs written after it (stb's default 8; 1: fast, for frame dumps)
// Linear float RGB(A) image; channels 3 or 4.
bool writeEXR(const std::string& path, int w, int h, int channels, const float* data);
bool readPNGGray(const std::string& path, int& w, int& h, std::vector<unsigned char>& out);
bool readEXR(const std::string& path, int& w, int& h, std::vector<float>& rgba);
