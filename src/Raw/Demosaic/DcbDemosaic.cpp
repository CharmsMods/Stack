/*
 *    Copyright (C) 2010,  Jacek Gozdz (cuniek@kft.umcs.lublin.pl)
 *
 *    This code is licensed under a (3-clause) BSD license as follows :
 *
 *    Redistribution and use in source and binary forms, with or without
 *    modification, are permitted provided that the following
 *	  conditions are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *		disclaimer in the documentation and/or other materials provided
 * 	    with the distribution.
 *    * Neither the name of the author nor the names of its
 *      contributors may be used to endorse or promote products
 * 		derived from this software without specific prior written permission.
 *
 *    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * 	  FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * 	  THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * 	  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * 	  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * 	  SERVICES; LOSS OF USE,
 *    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * 	  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *	  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * 	  OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 *    OF SUCH DAMAGE.
 */

// Adapted from LibRaw 0.22.1 dcb_demosaic.cpp (original BSD implementation).
// Stack changes: float sample storage without sensor-range clipping; RAII;
// cancellation; signed-sample ratio fallback; explicit input and output.
#include "FloatDemosaicInternal.h"

namespace Raw::Demosaic::Internal {
namespace {
template<class T> float CLIP(T value) { return static_cast<float>(value); }
template<class T> auto ABS(T value) { return std::abs(value); }
template<class T> T MIN(T a, T b) { return std::min(a,b); }
template<class T> T MAX(T a, T b) { return std::max(a,b); }
float ULIM(float value,float a,float b) { return std::clamp(value,std::min(a,b),std::max(a,b)); }
class DcbWorker {
public:
    explicit DcbWorker(const Input& input) : in(input), width(input.width), height(input.height),
        image(input.samples.size()) {
        for(int y=0;y<height;++y) {
            CheckRow(y);
            for(int x=0;x<width;++x)
                image[static_cast<std::size_t>(y)*width+x][FC(y,x)]=65535.f*in.At(x,y);
        }
    }
    void dcb(int iterations,int enhance);
    const Input& in;
    int width,height;
    std::vector<std::array<float,4>> image;
private:
    int FC(int row,int col) const { return in.Cfa(col,row); }
    void CheckRow(int row) const { in.Check(row); }
    void border_interpolate(int border) {
        for(int y=0;y<height;++y) {
            CheckRow(y);
            for(int x=0;x<width;++x) {
                if(x>=border && x<width-border && y>=border && y<height-border) continue;
                for(int c=0;c<3;++c) {
                    if(FC(y,x)==c) continue;
                    float sum=0; int n=0;
                    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
                        if(FC(y+dy,x+dx)==c) { sum+=in.At(x+dx,y+dy); ++n; }
                    image[static_cast<std::size_t>(y)*width+x][c]=65535.f*sum/n;
                }
            }
        }
    }
    void dcb_ver(std::array<float,3>*);
    void dcb_hor(std::array<float,3>*);
    void dcb_color();
    void dcb_color2(std::array<float,3>*);
    void dcb_color3(std::array<float,3>*);
    void dcb_decide(std::array<float,3>*,std::array<float,3>*);
    void dcb_copy_to_buffer(std::array<float,3>*);
    void dcb_restore_from_buffer(std::array<float,3>*);
    void dcb_pp();
    void dcb_nyquist();
    void dcb_color_full();
    void dcb_map();
    void dcb_correction();
    void dcb_correction2();
    void dcb_refinement();
};
// interpolates green vertically and saves it to image3
void DcbWorker::dcb_ver(std::array<float,3>* image3)
{
  int row, col, u = width, indx;

  for (row = 2; (CheckRow(row), row < height - 2); row++)
    for (col = 2 + (FC(row, 2) & 1), indx = row * width + col; col < u - 2;
         col += 2, indx += 2)
    {

      image3[indx][1] = float(CLIP((image[indx + u][1] + image[indx - u][1]) / 2.0));
    }
}

// interpolates green horizontally and saves it to image2
void DcbWorker::dcb_hor(std::array<float,3>* image2)
{
  int row, col, u = width, indx;

  for (row = 2; (CheckRow(row), row < height - 2); row++)
    for (col = 2 + (FC(row, 2) & 1), indx = row * width + col; col < u - 2;
         col += 2, indx += 2)
    {

      image2[indx][1] = float(CLIP((image[indx + 1][1] + image[indx - 1][1]) / 2.0));
    }
}

// missing colors are interpolated
void DcbWorker::dcb_color()
{
  int row, col, c, d, u = width, indx;

  for (row = 1; (CheckRow(row), row < height - 1); row++)
    for (col = 1 + (FC(row, 1) & 1), indx = row * width + col,
        c = 2 - FC(row, col);
         col < u - 1; col += 2, indx += 2)
    {

      image[indx][c] = CLIP((4 * image[indx][1] - image[indx + u + 1][1] -
                             image[indx + u - 1][1] - image[indx - u + 1][1] -
                             image[indx - u - 1][1] + image[indx + u + 1][c] +
                             image[indx + u - 1][c] + image[indx - u + 1][c] +
                             image[indx - u - 1][c]) /
                            4.0);
    }

  for (row = 1; (CheckRow(row), row < height - 1); row++)
    for (col = 1 + (FC(row, 2) & 1), indx = row * width + col,
        c = FC(row, col + 1), d = 2 - c;
         col < width - 1; col += 2, indx += 2)
    {

      image[indx][c] =
          CLIP((2 * image[indx][1] - image[indx + 1][1] - image[indx - 1][1] +
                image[indx + 1][c] + image[indx - 1][c]) /
               2.0);
      image[indx][d] =
          CLIP((2 * image[indx][1] - image[indx + u][1] - image[indx - u][1] +
                image[indx + u][d] + image[indx - u][d]) /
               2.0);
    }
}

// missing R and B are interpolated horizontally and saved in image2
void DcbWorker::dcb_color2(std::array<float,3>* image2)
{
  int row, col, c, d, u = width, indx;

  for (row = 1; (CheckRow(row), row < height - 1); row++)
    for (col = 1 + (FC(row, 1) & 1), indx = row * width + col,
        c = 2 - FC(row, col);
         col < u - 1; col += 2, indx += 2)
    {

      image2[indx][c] =
		  float(
          CLIP((4 * image2[indx][1] - image2[indx + u + 1][1] -
                image2[indx + u - 1][1] - image2[indx - u + 1][1] -
                image2[indx - u - 1][1] + image[indx + u + 1][c] +
                image[indx + u - 1][c] + image[indx - u + 1][c] +
                image[indx - u - 1][c]) /
               4.0)
			  );
    }

  for (row = 1; (CheckRow(row), row < height - 1); row++)
    for (col = 1 + (FC(row, 2) & 1), indx = row * width + col,
        c = FC(row, col + 1), d = 2 - c;
         col < width - 1; col += 2, indx += 2)
    {

      image2[indx][c] = float(CLIP((image[indx + 1][c] + image[indx - 1][c]) / 2.0));
      image2[indx][d] =
          float(CLIP((2 * image2[indx][1] - image2[indx + u][1] -
                image2[indx - u][1] + image[indx + u][d] + image[indx - u][d]) /
               2.0));
    }
}

// missing R and B are interpolated vertically and saved in image3
void DcbWorker::dcb_color3(std::array<float,3>* image3)
{
  int row, col, c, d, u = width, indx;

  for (row = 1; (CheckRow(row), row < height - 1); row++)
    for (col = 1 + (FC(row, 1) & 1), indx = row * width + col,
        c = 2 - FC(row, col);
         col < u - 1; col += 2, indx += 2)
    {

      image3[indx][c] =
		  float(
          CLIP((4 * image3[indx][1] - image3[indx + u + 1][1] -
                image3[indx + u - 1][1] - image3[indx - u + 1][1] -
                image3[indx - u - 1][1] + image[indx + u + 1][c] +
                image[indx + u - 1][c] + image[indx - u + 1][c] +
                image[indx - u - 1][c]) /
               4.0)
			  );
    }

  for (row = 1; (CheckRow(row), row < height - 1); row++)
    for (col = 1 + (FC(row, 2) & 1), indx = row * width + col,
        c = FC(row, col + 1), d = 2 - c;
         col < width - 1; col += 2, indx += 2)
    {

      image3[indx][c] =
		  float(
          CLIP((2 * image3[indx][1] - image3[indx + 1][1] -
                image3[indx - 1][1] + image[indx + 1][c] + image[indx - 1][c]) /
               2.0)
			  );
      image3[indx][d] = float(CLIP((image[indx + u][d] + image[indx - u][d]) / 2.0));
    }
}

// decides the primary green interpolation direction
void DcbWorker::dcb_decide(std::array<float,3>* image2, std::array<float,3>* image3)
{
  int row, col, c, d, u = width, v = 2 * u, indx;
  float current, current2, current3;

  for (row = 2; (CheckRow(row), row < height - 2); row++)
    for (col = 2 + (FC(row, 2) & 1), indx = row * width + col, c = FC(row, col);
         col < u - 2; col += 2, indx += 2)
    {

      d = ABS(c - 2);

      current = float(MAX(image[indx + v][c],
                    MAX(image[indx - v][c],
                        MAX(image[indx - 2][c], image[indx + 2][c]))) -
                MIN(image[indx + v][c],
                    MIN(image[indx - v][c],
                        MIN(image[indx - 2][c], image[indx + 2][c]))) +
                MAX(image[indx + 1 + u][d],
                    MAX(image[indx + 1 - u][d],
                        MAX(image[indx - 1 + u][d], image[indx - 1 - u][d]))) -
                MIN(image[indx + 1 + u][d],
                    MIN(image[indx + 1 - u][d],
                        MIN(image[indx - 1 + u][d], image[indx - 1 - u][d]))));

      current2 =
		  float(
          MAX(image2[indx + v][d],
              MAX(image2[indx - v][d],
                  MAX(image2[indx - 2][d], image2[indx + 2][d]))) -
          MIN(image2[indx + v][d],
              MIN(image2[indx - v][d],
                  MIN(image2[indx - 2][d], image2[indx + 2][d]))) +
          MAX(image2[indx + 1 + u][c],
              MAX(image2[indx + 1 - u][c],
                  MAX(image2[indx - 1 + u][c], image2[indx - 1 - u][c]))) -
          MIN(image2[indx + 1 + u][c],
              MIN(image2[indx + 1 - u][c],
                  MIN(image2[indx - 1 + u][c], image2[indx - 1 - u][c])))
			  );

      current3 =
		  float(
          MAX(image3[indx + v][d],
              MAX(image3[indx - v][d],
                  MAX(image3[indx - 2][d], image3[indx + 2][d]))) -
          MIN(image3[indx + v][d],
              MIN(image3[indx - v][d],
                  MIN(image3[indx - 2][d], image3[indx + 2][d]))) +
          MAX(image3[indx + 1 + u][c],
              MAX(image3[indx + 1 - u][c],
                  MAX(image3[indx - 1 + u][c], image3[indx - 1 - u][c]))) -
          MIN(image3[indx + 1 + u][c],
              MIN(image3[indx + 1 - u][c],
                  MIN(image3[indx - 1 + u][c], image3[indx - 1 - u][c])))
			  );

      if (ABS(current - current2) < ABS(current - current3))
        image[indx][1] = float(image2[indx][1]);
      else
        image[indx][1] = float(image3[indx][1]);
    }
}

// saves red and blue in image2
void DcbWorker::dcb_copy_to_buffer(std::array<float,3>* image2)
{
  int indx;

  for (indx = 0; indx < height * width; indx++)
  {
    image2[indx][0] = image[indx][0]; // R
    image2[indx][2] = image[indx][2]; // B
  }
}

// restores red and blue from image2
void DcbWorker::dcb_restore_from_buffer(std::array<float,3>* image2)
{
  int indx;

  for (indx = 0; indx < height * width; indx++)
  {
    image[indx][0] = float(image2[indx][0]); // R
    image[indx][2] = float(image2[indx][2]); // B
  }
}

// R and B smoothing using green contrast, all pixels except 2 pixel wide border
void DcbWorker::dcb_pp()
{
  float g1, r1, b1;
  int u = width, indx, row, col;

  for (row = 2; (CheckRow(row), row < height - 2); row++)
    for (col = 2, indx = row * u + col; col < width - 2; col++, indx++)
    {

      r1 = float((image[indx - 1][0] + image[indx + 1][0] + image[indx - u][0] +
            image[indx + u][0] + image[indx - u - 1][0] +
            image[indx + u + 1][0] + image[indx - u + 1][0] +
            image[indx + u - 1][0]) /
           8.0f);
      g1 = float((image[indx - 1][1] + image[indx + 1][1] + image[indx - u][1] +
            image[indx + u][1] + image[indx - u - 1][1] +
            image[indx + u + 1][1] + image[indx - u + 1][1] +
            image[indx + u - 1][1]) /
           8.0f);
      b1 = float((image[indx - 1][2] + image[indx + 1][2] + image[indx - u][2] +
            image[indx + u][2] + image[indx - u - 1][2] +
            image[indx + u + 1][2] + image[indx - u + 1][2] +
            image[indx + u - 1][2]) /
           8.0f);

      image[indx][0] = CLIP(r1 + (image[indx][1] - g1));
      image[indx][2] = CLIP(b1 + (image[indx][1] - g1));
    }
}

// green blurring correction, helps to get the nyquist right
void DcbWorker::dcb_nyquist()
{
  int row, col, c, u = width, v = 2 * u, indx;

  for (row = 2; (CheckRow(row), row < height - 2); row++)
    for (col = 2 + (FC(row, 2) & 1), indx = row * width + col, c = FC(row, col);
         col < u - 2; col += 2, indx += 2)
    {

      image[indx][1] = CLIP((image[indx + v][1] + image[indx - v][1] +
                             image[indx - 2][1] + image[indx + 2][1]) /
                                4.0 +
                            image[indx][c] -
                            (image[indx + v][c] + image[indx - v][c] +
                             image[indx - 2][c] + image[indx + 2][c]) /
                                4.0);
    }
}

// missing colors are interpolated using high quality algorithm by Luis Sanz
// Rodríguez
void DcbWorker::dcb_color_full()
{
  int row, col, c, d, u = width, w = 3 * u, indx;
  float g1, g2;
  float f[4], g[4];
  std::vector<std::array<float,2>> chroma(static_cast<std::size_t>(width)*height);

  for (row = 1; (CheckRow(row), row < height - 1); row++)
    for (col = 1 + (FC(row, 1) & 1), indx = row * width + col, c = FC(row, col),
        d = c / 2;
         col < u - 1; col += 2, indx += 2)
      chroma[indx][d] = float(image[indx][c] - image[indx][1]);

  for (row = 3; (CheckRow(row), row < height - 3); row++)
    for (col = 3 + (FC(row, 1) & 1), indx = row * width + col,
        c = 1 - FC(row, col) / 2, d = 1 - c;
         col < u - 3; col += 2, indx += 2)
    {
      f[0] = 1.0f /
             (float)(1.0 +
                     fabsf(chroma[indx - u - 1][c] - chroma[indx + u + 1][c]) +
                     fabsf(chroma[indx - u - 1][c] - chroma[indx - w - 3][c]) +
                     fabsf(chroma[indx + u + 1][c] - chroma[indx - w - 3][c]));
      f[1] = 1.0f /
             (float)(1.0 +
                     fabsf(chroma[indx - u + 1][c] - chroma[indx + u - 1][c]) +
                     fabsf(chroma[indx - u + 1][c] - chroma[indx - w + 3][c]) +
                     fabsf(chroma[indx + u - 1][c] - chroma[indx - w + 3][c]));
      f[2] = 1.0f /
             (float)(1.0 +
                     fabsf(chroma[indx + u - 1][c] - chroma[indx - u + 1][c]) +
                     fabsf(chroma[indx + u - 1][c] - chroma[indx + w + 3][c]) +
                     fabsf(chroma[indx - u + 1][c] - chroma[indx + w - 3][c]));
      f[3] = 1.0f /
             (float)(1.0 +
                     fabsf(chroma[indx + u + 1][c] - chroma[indx - u - 1][c]) +
                     fabsf(chroma[indx + u + 1][c] - chroma[indx + w - 3][c]) +
                     fabsf(chroma[indx - u - 1][c] - chroma[indx + w + 3][c]));
      g[0] = 1.325f * chroma[indx - u - 1][c] - 0.175f * chroma[indx - w - 3][c] -
             0.075f * chroma[indx - w - 1][c] - 0.075f * chroma[indx - u - 3][c];
      g[1] = 1.325f * chroma[indx - u + 1][c] - 0.175f * chroma[indx - w + 3][c] -
             0.075f * chroma[indx - w + 1][c] - 0.075f * chroma[indx - u + 3][c];
      g[2] = 1.325f * chroma[indx + u - 1][c] - 0.175f * chroma[indx + w - 3][c] -
             0.075f * chroma[indx + w - 1][c] - 0.075f * chroma[indx + u - 3][c];
      g[3] = 1.325f * chroma[indx + u + 1][c] - 0.175f * chroma[indx + w + 3][c] -
             0.075f * chroma[indx + w + 1][c] - 0.075f * chroma[indx + u + 3][c];
      chroma[indx][c] =
          (f[0] * g[0] + f[1] * g[1] + f[2] * g[2] + f[3] * g[3]) /
          (f[0] + f[1] + f[2] + f[3]);
    }
  for (row = 3; (CheckRow(row), row < height - 3); row++)
    for (col = 3 + (FC(row, 2) & 1), indx = row * width + col,
        c = FC(row, col + 1) / 2;
         col < u - 3; col += 2, indx += 2)
      for (d = 0; d <= 1; c = 1 - c, d++)
      {
        f[0] = 1.0f /
               (float)(1.0f + fabsf(chroma[indx - u][c] - chroma[indx + u][c]) +
                       fabsf(chroma[indx - u][c] - chroma[indx - w][c]) +
                       fabsf(chroma[indx + u][c] - chroma[indx - w][c]));
        f[1] = 1.0f /
               (float)(1.0f + fabsf(chroma[indx + 1][c] - chroma[indx - 1][c]) +
                       fabsf(chroma[indx + 1][c] - chroma[indx + 3][c]) +
                       fabsf(chroma[indx - 1][c] - chroma[indx + 3][c]));
        f[2] = 1.0f /
               (float)(1.0 + fabs(chroma[indx - 1][c] - chroma[indx + 1][c]) +
                       fabs(chroma[indx - 1][c] - chroma[indx - 3][c]) +
                       fabs(chroma[indx + 1][c] - chroma[indx - 3][c]));
        f[3] = 1.0f /
               (float)(1.0 + fabs(chroma[indx + u][c] - chroma[indx - u][c]) +
                       fabs(chroma[indx + u][c] - chroma[indx + w][c]) +
                       fabs(chroma[indx - u][c] - chroma[indx + w][c]));

        g[0] = 0.875f * chroma[indx - u][c] + 0.125f * chroma[indx - w][c];
        g[1] = 0.875f * chroma[indx + 1][c] + 0.125f * chroma[indx + 3][c];
        g[2] = 0.875f * chroma[indx - 1][c] + 0.125f * chroma[indx - 3][c];
        g[3] = 0.875f * chroma[indx + u][c] + 0.125f * chroma[indx + w][c];

        chroma[indx][c] =
            (f[0] * g[0] + f[1] * g[1] + f[2] * g[2] + f[3] * g[3]) /
            (f[0] + f[1] + f[2] + f[3]);
      }

  for (row = 6; (CheckRow(row), row < height - 6); row++)
    for (col = 6, indx = row * width + col; col < width - 6; col++, indx++)
    {
      image[indx][0] = CLIP(chroma[indx][0] + image[indx][1]);
      image[indx][2] = CLIP(chroma[indx][1] + image[indx][1]);

      g1 = MIN(
          image[indx + 1 + u][0],
          MIN(image[indx + 1 - u][0],
              MIN(image[indx - 1 + u][0],
                  MIN(image[indx - 1 - u][0],
                      MIN(image[indx - 1][0],
                          MIN(image[indx + 1][0],
                              MIN(image[indx - u][0], image[indx + u][0])))))));

      g2 = MAX(
          image[indx + 1 + u][0],
          MAX(image[indx + 1 - u][0],
              MAX(image[indx - 1 + u][0],
                  MAX(image[indx - 1 - u][0],
                      MAX(image[indx - 1][0],
                          MAX(image[indx + 1][0],
                              MAX(image[indx - u][0], image[indx + u][0])))))));

      image[indx][0] = ULIM(image[indx][0], g2, g1);

      g1 = MIN(
          image[indx + 1 + u][2],
          MIN(image[indx + 1 - u][2],
              MIN(image[indx - 1 + u][2],
                  MIN(image[indx - 1 - u][2],
                      MIN(image[indx - 1][2],
                          MIN(image[indx + 1][2],
                              MIN(image[indx - u][2], image[indx + u][2])))))));

      g2 = MAX(
          image[indx + 1 + u][2],
          MAX(image[indx + 1 - u][2],
              MAX(image[indx - 1 + u][2],
                  MAX(image[indx - 1 - u][2],
                      MAX(image[indx - 1][2],
                          MAX(image[indx + 1][2],
                              MAX(image[indx - u][2], image[indx + u][2])))))));

      image[indx][2] = ULIM(image[indx][2], g2, g1);
    }


}

// green is used to create an interpolation direction map saved in image[][3]
// 1 = vertical
// 0 = horizontal
void DcbWorker::dcb_map()
{
  int row, col, u = width, indx;

  for (row = 1; (CheckRow(row), row < height - 1); row++)
  {
    for (col = 1, indx = row * width + col; col < width - 1; col++, indx++)
    {

      if (image[indx][1] > (image[indx - 1][1] + image[indx + 1][1] +
                            image[indx - u][1] + image[indx + u][1]) /
                               4.0)
        image[indx][3] = ((MIN(image[indx - 1][1], image[indx + 1][1]) +
                           image[indx - 1][1] + image[indx + 1][1]) <
                          (MIN(image[indx - u][1], image[indx + u][1]) +
                           image[indx - u][1] + image[indx + u][1]));
      else
        image[indx][3] = ((MAX(image[indx - 1][1], image[indx + 1][1]) +
                           image[indx - 1][1] + image[indx + 1][1]) >
                          (MAX(image[indx - u][1], image[indx + u][1]) +
                           image[indx - u][1] + image[indx + u][1]));
    }
  }
}

// interpolated green pixels are corrected using the map
void DcbWorker::dcb_correction()
{
  int current, row, col, u = width, v = 2 * u, indx;

  for (row = 2; (CheckRow(row), row < height - 2); row++)
    for (col = 2 + (FC(row, 2) & 1), indx = row * width + col; col < u - 2;
         col += 2, indx += 2)
    {

      current = 4 * image[indx][3] +
                2 * (image[indx + u][3] + image[indx - u][3] +
                     image[indx + 1][3] + image[indx - 1][3]) +
                image[indx + v][3] + image[indx - v][3] + image[indx + 2][3] +
                image[indx - 2][3];

      image[indx][1] =
		  float(
          ((16 - current) * (image[indx - 1][1] + image[indx + 1][1]) / 2.0 +
           current * (image[indx - u][1] + image[indx + u][1]) / 2.0) /
          16.0f);
    }
}

// interpolated green pixels are corrected using the map
// with contrast correction
void DcbWorker::dcb_correction2()
{
  int current, row, col, c, u = width, v = 2 * u, indx;

  for (row = 4; (CheckRow(row), row < height - 4); row++)
    for (col = 4 + (FC(row, 2) & 1), indx = row * width + col, c = FC(row, col);
         col < u - 4; col += 2, indx += 2)
    {

      current = 4 * image[indx][3] +
                2 * (image[indx + u][3] + image[indx - u][3] +
                     image[indx + 1][3] + image[indx - 1][3]) +
                image[indx + v][3] + image[indx - v][3] + image[indx + 2][3] +
                image[indx - 2][3];

      image[indx][1] = CLIP(
          ((16 - current) * ((image[indx - 1][1] + image[indx + 1][1]) / 2.0 +
                             image[indx][c] -
                             (image[indx + 2][c] + image[indx - 2][c]) / 2.0) +
           current * ((image[indx - u][1] + image[indx + u][1]) / 2.0 +
                      image[indx][c] -
                      (image[indx + v][c] + image[indx - v][c]) / 2.0)) /
          16.0);
    }
}

void DcbWorker::dcb_refinement()
{
  int row, col, c, u = width, v = 2 * u, w = 3 * u, indx, current;
  float f[5], g1, g2;

  for (row = 4; (CheckRow(row), row < height - 4); row++)
    for (col = 4 + (FC(row, 2) & 1), indx = row * width + col, c = FC(row, col);
         col < u - 4; col += 2, indx += 2)
    {

      current = 4 * image[indx][3] +
                2 * (image[indx + u][3] + image[indx - u][3] +
                     image[indx + 1][3] + image[indx - 1][3]) +
                image[indx + v][3] + image[indx - v][3] + image[indx - 2][3] +
                image[indx + 2][3];

      if (image[indx][c] > 1)
      {

        f[0] = (float)(image[indx - u][1] + image[indx + u][1]) /
               (2 * image[indx][c]);

        if (image[indx - v][c] > 0)
          f[1] = 2 * (float)image[indx - u][1] /
                 (image[indx - v][c] + image[indx][c]);
        else
          f[1] = f[0];

        if (image[indx - v][c] > 0)
          f[2] = (float)(image[indx - u][1] + image[indx - w][1]) /
                 (2 * image[indx - v][c]);
        else
          f[2] = f[0];

        if (image[indx + v][c] > 0)
          f[3] = 2 * (float)image[indx + u][1] /
                 (image[indx + v][c] + image[indx][c]);
        else
          f[3] = f[0];

        if (image[indx + v][c] > 0)
          f[4] = (float)(image[indx + u][1] + image[indx + w][1]) /
                 (2 * image[indx + v][c]);
        else
          f[4] = f[0];

        g1 = (5.f * f[0] + 3.f * f[1] + f[2] + 3.f * f[3] + f[4]) / 13.0f;

        f[0] = (float)(image[indx - 1][1] + image[indx + 1][1]) /
               (2 * image[indx][c]);

        if (image[indx - 2][c] > 0)
          f[1] = 2 * (float)image[indx - 1][1] /
                 (image[indx - 2][c] + image[indx][c]);
        else
          f[1] = f[0];

        if (image[indx - 2][c] > 0)
          f[2] = (float)(image[indx - 1][1] + image[indx - 3][1]) /
                 (2 * image[indx - 2][c]);
        else
          f[2] = f[0];

        if (image[indx + 2][c] > 0)
          f[3] = 2 * (float)image[indx + 1][1] /
                 (image[indx + 2][c] + image[indx][c]);
        else
          f[3] = f[0];

        if (image[indx + 2][c] > 0)
          f[4] = (float)(image[indx + 1][1] + image[indx + 3][1]) /
                 (2 * image[indx + 2][c]);
        else
          f[4] = f[0];

        g2 = (5.f * f[0] + 3.f * f[1] + f[2] + 3.f * f[3] + f[4]) / 13.0f;

        image[indx][1] = CLIP((image[indx][c]) *
                              (current * g1 + (16 - current) * g2) / 16.0);
      }


      // get rid of overshooted pixels

      g1 = MIN(
          image[indx + 1 + u][1],
          MIN(image[indx + 1 - u][1],
              MIN(image[indx - 1 + u][1],
                  MIN(image[indx - 1 - u][1],
                      MIN(image[indx - 1][1],
                          MIN(image[indx + 1][1],
                              MIN(image[indx - u][1], image[indx + u][1])))))));

      g2 = MAX(
          image[indx + 1 + u][1],
          MAX(image[indx + 1 - u][1],
              MAX(image[indx - 1 + u][1],
                  MAX(image[indx - 1 - u][1],
                      MAX(image[indx - 1][1],
                          MAX(image[indx + 1][1],
                              MAX(image[indx - u][1], image[indx + u][1])))))));

      image[indx][1] = float(ULIM(image[indx][1], g2, g1));
    }
}

void DcbWorker::dcb(int iterations, int dcb_enhance)
{

  int i = 1;

  std::vector<std::array<float,3>> buffer2(static_cast<std::size_t>(width)*height);
  std::vector<std::array<float,3>> buffer3(buffer2.size());
  auto* image2 = buffer2.data();
  auto* image3 = buffer3.data();

  border_interpolate(6);

  dcb_hor(image2);
  dcb_color2(image2);

  dcb_ver(image3);
  dcb_color3(image3);

  dcb_decide(image2, image3);

  std::vector<std::array<float,3>>().swap(buffer3);

  dcb_copy_to_buffer(image2);

  while (i <= iterations)
  {
    dcb_nyquist();
    dcb_nyquist();
    dcb_nyquist();
    dcb_map();
    dcb_correction();
    i++;
  }

  dcb_color();
  dcb_pp();

  dcb_map();
  dcb_correction2();

  dcb_map();
  dcb_correction();

  dcb_map();
  dcb_correction();

  dcb_map();
  dcb_correction();

  dcb_map();
  dcb_restore_from_buffer(image2);
  dcb_color();

  if (dcb_enhance)
  {
    dcb_refinement();
    // dcb_color_full(image2);
    dcb_color_full();
  }


}

} // namespace
void Dcb(const Input& in, bool enhanced, std::vector<float>& rgb) {
    DcbWorker worker(in);
    worker.dcb(3, enhanced ? 1 : 0);
    rgb.resize(in.samples.size()*3);
    for(int y=0;y<in.height;++y) {
        in.Check(y);
        for(int x=0;x<in.width;++x) {
            const auto i=static_cast<std::size_t>(y)*in.width+x;
            for(int c=0;c<3;++c) rgb[3*i+c]=worker.image[i][c]/65535.f;
            rgb[3*i+in.Cfa(x,y)]=in.samples[i];
        }
    }
}
} // namespace Raw::Demosaic::Internal
