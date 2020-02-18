/*=========================================================================
 *
 *  Copyright Insight Software Consortium
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/
/*=========================================================================

  Program: DICOM for VTK

  Copyright (c) 2015 David Gobbi
  All rights reserved.
  See Copyright.txt or http://dgobbi.github.io/bsd3.txt for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

#include "itkScancoImageIO.h"
#include "itkSpatialOrientationAdapter.h"
#include "itkIOCommon.h"
#include "itksys/SystemTools.hxx"
#include "itkMath.h"
#include "itkIntTypes.h"
#include "itkByteSwapper.h"

#include <algorithm>
#include <ctime>

namespace itk
{

ScancoImageIO
::ScancoImageIO():
  m_HeaderSize( 0 )
{
  this->m_FileType = IOFileEnum::Binary;
  this->m_ByteOrder = IOByteOrderEnum::LittleEndian;

  this->AddSupportedWriteExtension(".isq");
  this->AddSupportedWriteExtension(".rsq");
  this->AddSupportedWriteExtension(".rad");
  this->AddSupportedWriteExtension(".aim");

  this->AddSupportedReadExtension(".isq");
  this->AddSupportedReadExtension(".rsq");
  this->AddSupportedReadExtension(".rad");
  this->AddSupportedReadExtension(".aim");

  this->m_RawHeader = 0;
}


ScancoImageIO
::~ScancoImageIO()
{
  delete [] this->m_RawHeader;
}


void
ScancoImageIO
::PrintSelf(std::ostream & os, Indent indent) const
{
  Superclass::PrintSelf(os, indent);
}


int
ScancoImageIO
::CheckVersion(const char header[16])
{
  int fileType = 0;

  if (strncmp(header, "CTDATA-HEADER_V1", 16) == 0)
    {
    fileType = 1;
    }
  else if (strcmp(header, "AIMDATA_V030   ") == 0)
    {
    fileType = 3;
    }
  else
    {
    int preHeaderSize = ScancoImageIO::DecodeInt(header);
    int imageHeaderSize = ScancoImageIO::DecodeInt(header + 4);
    if (preHeaderSize == 20 && imageHeaderSize == 140)
      {
      fileType = 2;
      }
    }

  return fileType;
}


int
ScancoImageIO
::DecodeInt(const void *data)
{
  const unsigned char *cp = static_cast<const unsigned char *>(data);
  return (cp[0] | (cp[1] << 8) | (cp[2] << 16) | (cp[3] << 24));
}


void
ScancoImageIO
::EncodeInt(int data, void *target)
{
  unsigned char * targetAsUnsignedChar = static_cast< unsigned char * >( target );
  targetAsUnsignedChar[0] = (unsigned char)(data);
  targetAsUnsignedChar[1] = (unsigned char)(data >> 8);
  targetAsUnsignedChar[2] = (unsigned char)(data >> 16);
  targetAsUnsignedChar[3] = (unsigned char)(data >> 24);
}


float
ScancoImageIO
::DecodeFloat(const void *data)
{
  const unsigned char *cp = static_cast<const unsigned char *>(data);
  // different ordering and exponent bias than IEEE 754 float
  union { float f; unsigned int i; } v;
  v.i = (cp[0] << 16) | (cp[1] << 24) | cp[2] | (cp[3] << 8);
  return 0.25*v.f;
}


double
ScancoImageIO
::DecodeDouble(const void *data)
{
  // different ordering and exponent bias than IEEE 754 double
  const unsigned char *cp = static_cast<const unsigned char *>(data);
  union { double d; uint64_t l; } v;
  unsigned int l1, l2;
  l1 = (cp[0] << 16) | (cp[1] << 24) | cp[2] | (cp[3] << 8);
  l2 = (cp[4] << 16) | (cp[5] << 24) | cp[6] | (cp[7] << 8);
  v.l = (static_cast<uint64_t>(l1) << 32) | l2;
  return v.d*0.25;
}


void
ScancoImageIO
::DecodeDate(const void *data,
  int& year, int& month, int& day,
  int& hour, int& minute, int& second, int& millis)
{
  // This is the offset between the astronomical "Julian day", which counts
  // days since January 1, 4713BC, and the "VMS epoch", which counts from
  // November 17, 1858:
  const uint64_t julianOffset = 2400001;
  const uint64_t millisPerSecond = 1000;
  const uint64_t millisPerMinute = 60 * 1000;
  const uint64_t millisPerHour = 3600 * 1000;
  const uint64_t millisPerDay = 3600 * 24 * 1000;

  // Read the date as a long integer with units of 1e-7 seconds
  int d1 = ScancoImageIO::DecodeInt(data);
  int d2 = ScancoImageIO::DecodeInt(static_cast<const char *>(data)+4);
  uint64_t tVMS = d1 + (static_cast<uint64_t>(d2) << 32);
  uint64_t time = tVMS/10000 + julianOffset*millisPerDay;

  int y, m, d;
  int julianDay = static_cast<int>(time / millisPerDay);
  time -= millisPerDay*julianDay;

  // Gregorian calendar starting from October 15, 1582
  // This algorithm is from Henry F. Fliegel and Thomas C. Van Flandern
  int ell, n, i, j;
  ell = julianDay + 68569;
  n = (4 * ell) / 146097;
  ell = ell - (146097 * n + 3) / 4;
  i = (4000 * (ell + 1)) / 1461001;
  ell = ell - (1461 * i) / 4 + 31;
  j = (80 * ell) / 2447;
  d = ell - (2447 * j) / 80;
  ell = j / 11;
  m = j + 2 - (12 * ell);
  y = 100 * (n - 49) + i + ell;

  // Return the result
  year = y;
  month = m;
  day = d;
  hour = static_cast<int>(time / millisPerHour);
  time -= hour*millisPerHour;
  minute = static_cast<int>(time / millisPerMinute);
  time -= minute*millisPerMinute;
  second = static_cast<int>(time / millisPerSecond);
  time -= second*millisPerSecond;
  millis = static_cast<int>(time);
}


void
ScancoImageIO
::EncodeDate(void * target)
{
  time_t currentTimeUnix;
  std::time(&currentTimeUnix);
  const uint64_t currentTimeVMS = currentTimeUnix * 10000000 + 3506716800;

  const int d1 = (int)currentTimeVMS;
  const int d2 = (int)(currentTimeVMS >> 32);
  ScancoImageIO::EncodeInt( d1, target );
  ScancoImageIO::EncodeInt( d2, static_cast<char *>(target)+4 );
}


bool
ScancoImageIO
::CanReadFile(const char *filename)
{
  std::ifstream infile;
  this->OpenFileForReading( infile, filename );

  bool canRead = false;
  if (infile.good())
    {
    // header is a 512 byte block
    char buffer[512];
    infile.read(buffer, 512);
    if (!infile.bad())
      {
      int fileType = ScancoImageIO::CheckVersion(buffer);
      canRead = (fileType > 0);
      }
    }

  infile.close();

  return canRead;
}


void
ScancoImageIO
::InitializeHeader()
{
  memset(this->m_Version, 0, 18);
  memset(this->m_PatientName, 0, 42);
  memset(this->CreationDate, 0, 32);
  memset(this->ModificationDate, 0, 32);
  this->ScanDimensionsPixels[0] = 0;
  this->ScanDimensionsPixels[1] = 0;
  this->ScanDimensionsPixels[2] = 0;
  this->ScanDimensionsPhysical[0] = 0;
  this->ScanDimensionsPhysical[1] = 0;
  this->ScanDimensionsPhysical[2] = 0;
  this->m_PatientIndex = 0;
  this->m_ScannerID = 0;
  this->m_SliceThickness = 0;
  this->m_SliceIncrement = 0;
  this->m_StartPosition = 0;
  this->m_EndPosition = 0;
  this->m_ZPosition = 0;
  this->m_DataRange[0] = 0;
  this->m_DataRange[1] = 0;
  this->m_MuScaling = 1.0;
  this->m_NumberOfSamples = 0;
  this->m_NumberOfProjections = 0;
  this->m_ScanDistance = 0;
  this->m_SampleTime = 0;
  this->m_ScannerType = 0;
  this->m_MeasurementIndex = 0;
  this->m_Site = 0;
  this->m_ReconstructionAlg = 0;
  this->m_ReferenceLine = 0;
  this->m_Energy = 0;
  this->m_Intensity = 0;

  this->m_RescaleType = 0;
  memset(this->m_RescaleUnits, 0, 18);
  memset(this->m_CalibrationData, 0, 66);
  this->m_RescaleSlope = 1.0;
  this->m_RescaleIntercept = 0.0;
  this->m_MuWater = 0;

  this->m_Compression = 0;
}


void
ScancoImageIO
::StripString(char *dest, const char *source, size_t length)
{
  char *dp = dest;
  for (size_t i = 0; i < length && *source != '\0'; ++i)
    {
    *dp++ = *source++;
    }
  while (dp != dest && dp[-1] == ' ')
    {
    --dp;
    }
  *dp = '\0';
}


void
ScancoImageIO
::PadString(char *dest, const char *source, size_t length)
{
  for (size_t i = 0; i < length && *source != '\0'; ++i)
    {
    *dest++ = *source++;
    }
  for (size_t i = 0; i < length; ++i)
    {
    *dest++ = ' ';
    }
}


int
ScancoImageIO
::ReadISQHeader(std::ifstream *file, unsigned long bytesRead)
{
  if (bytesRead < 512)
    {
    return 0;
    }

  char *h = this->m_RawHeader;
  ScancoImageIO::StripString(this->m_Version, h, 16); h += 16;
  int dataType = ScancoImageIO::DecodeInt(h); h += 4;
  const int numBytes = ScancoImageIO::DecodeInt(h); h += 4;
  (void) numBytes;
  const int numBlocks = ScancoImageIO::DecodeInt(h); h += 4;
  (void) numBlocks;
  this->m_PatientIndex = ScancoImageIO::DecodeInt(h); h += 4;
  this->m_ScannerID = ScancoImageIO::DecodeInt(h); h += 4;
  int year, month, day, hour, minute, second, milli;
  ScancoImageIO::DecodeDate(
    h, year, month, day, hour, minute, second, milli); h += 8;
  int pixdim[3], physdim[3];
  pixdim[0] = ScancoImageIO::DecodeInt(h); h += 4;
  pixdim[1] = ScancoImageIO::DecodeInt(h); h += 4;
  pixdim[2] = ScancoImageIO::DecodeInt(h); h += 4;
  physdim[0] = ScancoImageIO::DecodeInt(h); h += 4;
  physdim[1] = ScancoImageIO::DecodeInt(h); h += 4;
  physdim[2] = ScancoImageIO::DecodeInt(h); h += 4;

  const bool isRAD = (dataType == 9 || physdim[2] == 0);

  if (isRAD) // RAD file
    {
    this->m_MeasurementIndex = ScancoImageIO::DecodeInt(h); h += 4;
    this->m_DataRange[0] = ScancoImageIO::DecodeInt(h); h += 4;
    this->m_DataRange[1] = ScancoImageIO::DecodeInt(h); h += 4;
    this->m_MuScaling = ScancoImageIO::DecodeInt(h); h += 4;
    ScancoImageIO::StripString(this->m_PatientName, h, 40); h += 40;
    this->m_ZPosition = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    /* unknown */ h += 4;
    this->m_SampleTime = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->m_Energy = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->m_Intensity = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->m_ReferenceLine = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->m_StartPosition = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->m_EndPosition = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    h += 88*4;
    }
  else // ISQ file or RSQ file
    {
    this->m_SliceThickness = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->m_SliceIncrement = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->m_StartPosition = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->m_EndPosition =
      this->m_StartPosition + physdim[2]*1e-3*(pixdim[2] - 1)/pixdim[2];
    this->m_DataRange[0] = ScancoImageIO::DecodeInt(h); h += 4;
    this->m_DataRange[1] = ScancoImageIO::DecodeInt(h); h += 4;
    this->m_MuScaling = ScancoImageIO::DecodeInt(h); h += 4;
    this->m_NumberOfSamples = ScancoImageIO::DecodeInt(h); h += 4;
    this->m_NumberOfProjections = ScancoImageIO::DecodeInt(h); h += 4;
    this->m_ScanDistance = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->m_ScannerType = ScancoImageIO::DecodeInt(h); h += 4;
    this->m_SampleTime = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->m_MeasurementIndex = ScancoImageIO::DecodeInt(h); h += 4;
    this->m_Site = ScancoImageIO::DecodeInt(h); h += 4;
    this->m_ReferenceLine = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->m_ReconstructionAlg = ScancoImageIO::DecodeInt(h); h += 4;
    ScancoImageIO::StripString(this->m_PatientName, h, 40); h += 40;
    this->m_Energy = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    this->m_Intensity = ScancoImageIO::DecodeInt(h)*1e-3; h += 4;
    h += 83*4;
    }

  int dataOffset = ScancoImageIO::DecodeInt(h);

  // fix m_SliceThickness and m_SliceIncrement if they were truncated
  if (physdim[2] != 0)
    {
    double computedSpacing = physdim[2]*1e-3/pixdim[2];
    if (fabs(computedSpacing - this->m_SliceThickness) < 1.1e-3)
      {
      this->m_SliceThickness = computedSpacing;
      }
    if (fabs(computedSpacing - this->m_SliceIncrement) < 1.1e-3)
      {
      this->m_SliceIncrement = computedSpacing;
      }
    }

  // Convert date information into a string
  month = ((month > 12 || month < 1) ? 0 : month);
  static const char *months[] = { "XXX", "JAN", "FEB", "MAR", "APR", "MAY",
    "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
  sprintf(this->CreationDate, "%d-%s-%d %02d:%02d:%02d.%03d",
          (day % 100), months[month], (year % 10000),
          (hour % 100), (minute % 100), (second % 100), (milli % 1000));
  sprintf(this->ModificationDate, "%d-%s-%d %02d:%02d:%02d.%03d",
          (day % 100), months[month], (year % 10000),
          (hour % 100), (minute % 100), (second % 100), (milli % 1000));

  // Perform a sanity check on the dimensions
  for (int i = 0; i < 3; ++i)
    {
    this->ScanDimensionsPixels[i] = pixdim[i];
    if (pixdim[i] < 1)
      {
      pixdim[i] = 1;
      }
    this->ScanDimensionsPhysical[i] =
      (isRAD ? physdim[i]*1e-6 : physdim[i]*1e-3);
    if (physdim[i] == 0)
      {
      physdim[i] = 1.0;
      }
    }

  this->SetNumberOfDimensions( 3 );
  for ( unsigned int i = 0; i < m_NumberOfDimensions; ++i )
    {
    this->SetDimensions(i, pixdim[i] );
    if (isRAD) // RAD file
      {
      if( i == 2 )
        {
        this->SetSpacing( i, 1.0 );
        }
      else
        {
        this->SetSpacing( i, physdim[i]*1e-6/pixdim[i] );
        }
      }
    else
      {
      this->SetSpacing( i, physdim[i]*1e-3/pixdim[i] );
      }
    this->SetOrigin( i, 0.0 );
    }

  this->SetPixelType(IOPixelEnum::SCALAR);
  this->SetComponentType(IOComponentEnum::SHORT);

  // total header size
  const SizeValueType headerSize = static_cast< SizeValueType >(dataOffset + 1)*512;
  this->m_HeaderSize = headerSize;

  // read the rest of the header
  if (headerSize > bytesRead)
    {
    h = new char[headerSize];
    memcpy(h, this->m_RawHeader, bytesRead);
    delete [] this->m_RawHeader;
    this->m_RawHeader = h;
    file->read(h + bytesRead, headerSize - bytesRead);
    if (static_cast<unsigned long>(file->gcount()) < headerSize - bytesRead)
      {
      return 0;
      }
    }

  // decode the extended header (lots of guesswork)
  if (headerSize >= 2048)
    {
    char *calHeader = 0;
    int calHeaderSize = 0;
    h = this->m_RawHeader + 512;
    unsigned long hskip = 1;
    char *headerName = h + 8;
    if (strncmp(headerName, "MultiHeader     ", 16) == 0)
      {
      h += 512;
      hskip += 1;
      }
    unsigned long hsize = 0;
    for (int i = 0; i < 4; ++i)
      {
      hsize = ScancoImageIO::DecodeInt(h + i*128 + 24);
      if ((1 + hskip + hsize)*512 > headerSize)
        {
        break;
        }
      headerName = h + i*128 + 8;
      if (strncmp(headerName, "Calibration     ", 16) == 0)
        {
        calHeader = this->m_RawHeader + (1 + hskip)*512;
        calHeaderSize = hsize*512;
        }
      hskip += hsize;
      }

    if (calHeader && calHeaderSize >= 1024)
      {
      h = calHeader;
      ScancoImageIO::StripString(this->m_CalibrationData, h + 28, 64);
      // std::string calFile(h + 112, 256);
      // std::string s3(h + 376, 256);
      this->m_RescaleType = ScancoImageIO::DecodeInt(h + 632);
      ScancoImageIO::StripString(this->m_RescaleUnits, h + 648, 16);
      // std::string s5(h + 700, 16);
      // std::string calFilter(h + 772, 16);
      this->m_RescaleSlope = ScancoImageIO::DecodeDouble(h + 664);
      this->m_RescaleIntercept = ScancoImageIO::DecodeDouble(h + 672);
      this->m_MuWater = ScancoImageIO::DecodeDouble(h + 688);
      }
    }

  // Include conversion to linear att coeff in the rescaling
  if (this->m_MuScaling != 0)
    {
    this->m_RescaleSlope /= this->m_MuScaling;
    }

  return 1;
}


int
ScancoImageIO
::ReadAIMHeader(std::ifstream *file, unsigned long bytesRead)
{
  if (bytesRead < 160)
    {
    return 0;
    }

  char *h = this->m_RawHeader;
  int intSize = 0;
  unsigned long headerSize = 0;
  if (strcmp(h, "AIMDATA_V030   ") == 0)
    {
    // header uses 64-bit ints (8 bytes)
    intSize = 8;
    strcpy(this->m_Version, h);
    headerSize = 16;
    h += headerSize;
    }
  else
    {
    // header uses 32-bit ints (4 bytes)
    intSize = 4;
    strcpy(this->m_Version, "AIMDATA_V020   ");
    }

  // read the pre-header
  char *preheader = h;
  int preheaderSize = ScancoImageIO::DecodeInt(h); h += intSize;
  int structSize = ScancoImageIO::DecodeInt(h); h += intSize;
  int logSize = ScancoImageIO::DecodeInt(h); h += intSize;

  // read the rest of the header
  headerSize += preheaderSize + structSize + logSize;
  //this->SetHeaderSize(headerSize);
  if (headerSize > bytesRead)
    {
    h = new char[headerSize];
    memcpy(h, this->m_RawHeader, bytesRead);
    preheader = h + (preheader - this->m_RawHeader);
    delete [] this->m_RawHeader;
    this->m_RawHeader = h;
    file->read(h + bytesRead, headerSize - bytesRead);
    if (static_cast<unsigned long>(file->gcount()) < headerSize - bytesRead)
      {
      return 0;
      }
    }

  // decode the struct header
  h = preheader + preheaderSize;
  h += 20; // not sure what these 20 bytes are for
  int dataType = ScancoImageIO::DecodeInt(h); h += 4;
  int structValues[21];
  for (int i = 0; i < 21; ++i)
    {
    structValues[i] = ScancoImageIO::DecodeInt(h); h += intSize;
    }
  float elementSize[3];
  for (int i = 0; i < 3; ++i)
    {
    elementSize[i] = ScancoImageIO::DecodeFloat(h);
    if (elementSize[i] == 0)
      {
      elementSize[i] = 1.0;
      }
    h += 4;
    }

  // number of components per pixel is 1 by default
  this->SetPixelType( IOPixelEnum::SCALAR );
  this->m_Compression = 0;

  // a limited selection of data types are supported
  // (only 0x00010001 (char) and 0x00020002 (short) are fully tested)
  switch (dataType)
  {
    case 0x00160001:
      this->SetComponentType( IOComponentEnum::UCHAR );
      break;
    case 0x000d0001:
      this->SetComponentType( IOComponentEnum::UCHAR );
      break;
    case 0x00120003:
      this->SetComponentType( IOComponentEnum::UCHAR );
      this->SetPixelType( IOPixelEnum::VECTOR );
      this->SetNumberOfDimensions( 3 );
      break;
    case 0x00010001:
      this->SetComponentType( IOComponentEnum::CHAR );
      break;
    case 0x00060003:
      this->SetComponentType( IOComponentEnum::CHAR );
      this->SetPixelType( IOPixelEnum::VECTOR );
      this->SetNumberOfDimensions( 3 );
      break;
    case 0x00170002:
      this->SetComponentType( IOComponentEnum::USHORT );
      break;
    case 0x00020002:
      this->SetComponentType( IOComponentEnum::SHORT );
      break;
    case 0x00030004:
      this->SetComponentType( IOComponentEnum::INT );
      break;
    case 0x001a0004:
      this->SetComponentType( IOComponentEnum::FLOAT );
      break;
    case 0x00150001:
      this->m_Compression = 0x00b2; // run-length compressed bits
      this->SetComponentType( IOComponentEnum::CHAR );
      break;
    case 0x00080002:
      this->m_Compression = 0x00c2; // run-length compressed signed char
      this->SetComponentType( IOComponentEnum::CHAR );
      break;
    case 0x00060001:
      this->m_Compression = 0x00b1; // packed bits
      this->SetComponentType( IOComponentEnum::CHAR );
      break;
    default:
      itkExceptionMacro("Unrecognized data type in AIM file: " << dataType);
  }

  this->SetNumberOfDimensions( 3 );
  for ( unsigned int i = 0; i < m_NumberOfDimensions; ++i )
    {
    this->SetDimensions(i, structValues[3 + i] );
    this->SetSpacing( i, elementSize[i] );
    // the origin will reflect the cropping of the data
    this->SetOrigin( i, elementSize[i] * structValues[i] );
    }

  // decode the processing log
  h = preheader + preheaderSize + structSize;
  char *logEnd = h + logSize;

  while (h != logEnd && *h != '\0')
    {
    // skip newline and go to next line
    if (*h == '\n')
      {
      ++h;
      }

    // search for the end of this line
    char *lineEnd = h;
    while (lineEnd != logEnd && *lineEnd != '\n' && *lineEnd != '\0')
      {
      ++lineEnd;
      }

    // if not a comment, search for keys
    if (h != lineEnd && *h != '!' && (*lineEnd == '\n' || *lineEnd == '\0'))
      {
      // key and value are separated by multiple spaces
      char *key = h;
      while (h+1 != lineEnd && (h[0] != ' ' || h[1] != ' '))
        {
        ++h;
        }
      // this gives the length of the key
      size_t keylen = h - key;
      // skip to the end of the spaces
      while (h != lineEnd && *h == ' ')
        {
        ++h;
        }
      // this is where the value starts
      char *value = h;
      size_t valuelen = lineEnd - value;
      // look for trailing spaces
      while (valuelen > 0 && (h[valuelen-1] == ' ' || h[valuelen-1] == '\r'))
        {
        --valuelen;
        }

      // convert into a std::string for convenience
      std::string skey(key, keylen);

      // check for known keys
      if (skey == "Time")
        {
        valuelen = (valuelen > 31 ? 31 : valuelen);
        strncpy(this->ModificationDate, value, valuelen);
        this->ModificationDate[valuelen] = '\0';
        }
      else if (skey == "Original Creation-Date")
        {
        valuelen = (valuelen > 31 ? 31 : valuelen);
        strncpy(this->CreationDate, value, valuelen);
        this->CreationDate[valuelen] = '\0';
        }
      else if (skey == "Orig-ISQ-Dim-p")
        {
        for (int i = 0; i < 3; i++)
          {
          this->ScanDimensionsPixels[i] = strtol(value, &value, 10);
          }
        }
      else if (skey == "Orig-ISQ-Dim-um")
        {
        for (int i = 0; i < 3; i++)
          {
          this->ScanDimensionsPhysical[i] = strtod(value, &value)*1e-3;
          }
        }
      else if (skey == "Patient Name")
        {
        valuelen = (valuelen > 41 ? 41 : valuelen);
        strncpy(this->m_PatientName, value, valuelen);
        this->m_PatientName[valuelen] = '\0';
        }
      else if (skey == "Index Patient")
        {
        this->m_PatientIndex = strtol(value, 0, 10);
        }
      else if (skey == "Index Measurement")
        {
        this->m_MeasurementIndex = strtol(value, 0, 10);
        }
      else if (skey == "Site")
        {
        this->m_Site = strtol(value, 0, 10);
        }
      else if (skey == "Scanner ID")
        {
        this->m_ScannerID = strtol(value, 0, 10);
        }
      else if (skey == "Scanner type")
        {
        this->m_ScannerType = strtol(value, 0, 10);
        }
      else if (skey == "Position Slice 1 [um]")
        {
        this->m_StartPosition = strtod(value, 0)*1e-3;
        this->m_EndPosition =
          this->m_StartPosition + elementSize[2]*(structValues[5] - 1);
        }
      else if (skey == "No. samples")
        {
        this->m_NumberOfSamples = strtol(value, 0, 10);
        }
      else if (skey == "No. projections per 180")
        {
        this->m_NumberOfProjections = strtol(value, 0, 10);
        }
      else if (skey == "Scan Distance [um]")
        {
        this->m_ScanDistance = strtod(value, 0)*1e-3;
        }
      else if (skey == "Integration time [us]")
        {
        this->m_SampleTime = strtod(value, 0)*1e-3;
        }
      else if (skey == "Reference line [um]")
        {
        this->m_ReferenceLine = strtod(value, 0)*1e-3;
        }
      else if (skey == "Reconstruction-Alg.")
        {
        this->m_ReconstructionAlg = strtol(value, 0, 10);
        }
      else if (skey == "Energy [V]")
        {
        this->m_Energy = strtod(value, 0)*1e-3;
        }
      else if (skey == "Intensity [uA]")
        {
        this->m_Intensity = strtod(value, 0)*1e-3;
        }
      else if (skey == "Mu_Scaling")
        {
        this->m_MuScaling = strtol(value, 0, 10);
        }
      else if (skey == "Minimum data value")
        {
        this->m_DataRange[0] = strtod(value, 0);
        }
      else if (skey == "Maximum data value")
        {
        this->m_DataRange[1] = strtod(value, 0);
        }
      else if (skey == "Calib. default unit type")
        {
        this->m_RescaleType = strtol(value, 0, 10);
        }
      else if (skey == "Calibration Data")
        {
        valuelen = (valuelen > 64 ? 64 : valuelen);
        strncpy(this->m_CalibrationData, value, valuelen);
        this->m_CalibrationData[valuelen] = '\0';
        }
      else if (skey == "Density: unit")
        {
        valuelen = (valuelen > 16 ? 16 : valuelen);
        strncpy(this->m_RescaleUnits, value, valuelen);
        this->m_RescaleUnits[valuelen] = '\0';
        }
      else if (skey == "Density: slope")
        {
        this->m_RescaleSlope = strtod(value, 0);
        }
      else if (skey == "Density: intercept")
        {
        this->m_RescaleIntercept = strtod(value, 0);
        }
      else if (skey == "HU: mu water")
        {
        this->m_MuWater = strtod(value, 0);
        }
      }
    // skip to the end of the line
    h = lineEnd;
    }

  // Include conversion to linear att coeff in the rescaling
  if (this->m_MuScaling != 0)
    {
    this->m_RescaleSlope /= this->m_MuScaling;
    }

  // these items are not in the processing log
  this->m_SliceThickness = elementSize[2];
  this->m_SliceIncrement = elementSize[2];

  return 1;
}


void
ScancoImageIO
::ReadImageInformation()
{
  this->InitializeHeader();

  if( this->m_FileName.empty() )
    {
    itkExceptionMacro( "FileName has not been set." );
    }

  std::ifstream infile;
  this->OpenFileForReading( infile, this->m_FileName );

  // header is a 512 byte block
  this->m_RawHeader = new char[512];
  infile.read(this->m_RawHeader, 512);
  int fileType = 0;
  unsigned long bytesRead = 0;
  if (!infile.bad())
    {
    bytesRead = static_cast<unsigned long>(infile.gcount());
    fileType = ScancoImageIO::CheckVersion(this->m_RawHeader);
    }

  if (fileType == 0)
    {
    infile.close();
    itkExceptionMacro( "Unrecognized header in: " << m_FileName );
    }

  if (fileType == 1)
    {
    this->ReadISQHeader(&infile, bytesRead);
    }
  else
    {
    this->ReadAIMHeader(&infile, bytesRead);
    }

  infile.close();

  // This code causes rescaling to Hounsfield units
  /*
  if (this->m_MuScaling > 0 && this->m_MuWater > 0)
    {
    // HU = 1000*(u - u_water)/u_water
    this->m_RescaleSlope = 1000.0/(this->m_MuWater * this->m_MuScaling);
    this->m_RescaleIntercept = -1000.0;
    }
  */
}


void
ScancoImageIO
::Read(void *buffer)
{
  std::ifstream infile;
  this->OpenFileForReading( infile, this->m_FileName );

  // seek to the data
  infile.seekg(this->m_HeaderSize);

  // get the size of the compressed data
  int intSize = 4;
  if (strcmp(this->m_Version, "AIMDATA_V030   ") == 0)
    {
    // header uses 64-bit ints (8 bytes)
    intSize = 8;
    }

  // Dimensions of the data
  const int xsize = this->GetDimensions( 0 );
  const int ysize = this->GetDimensions( 1 );
  const int zsize = this->GetDimensions( 2 );
  size_t outSize = xsize;
  outSize *= ysize;
  outSize *= zsize;
  outSize *= this->GetComponentSize();

  // For the input (compressed) data
  char *input = 0;
  size_t size = 0;

  if(this->m_Compression == 0)
    {
    infile.read(reinterpret_cast< char * >( buffer ), outSize);
    return;
    }
  else if (this->m_Compression == 0x00b1)
    {
    // Compute the size of the binary packed data
    size_t xinc = (xsize+1)/2;
    size_t yinc = (ysize+1)/2;
    size_t zinc = (zsize+1)/2;
    size = xinc*yinc*zinc + 1;
    input = new char[size];
    infile.read(input, size);
    }
  else if (this->m_Compression == 0x00b2 ||
           this->m_Compression == 0x00c2)
    {
    // Get the size of the compressed data
    char head[8];
    infile.read(head, intSize);
    size = static_cast<unsigned int>(ScancoImageIO::DecodeInt(head));
    if (intSize == 8)
      {
      // Read the high word of a 64-bit int
      unsigned int high = ScancoImageIO::DecodeInt(head + 4);
      size += (static_cast< uint64_t >(high) << 32);
      }
    input = new char[size - intSize];
    size -= intSize;
    infile.read(input, size);
    }

  // confirm that enough data was read
  size_t shortread = size - infile.gcount();
  if (shortread != 0)
    {
    itkExceptionMacro("File is truncated, " << shortread << " bytes are missing");
    }

  // Close the file
  infile.close();

  unsigned char * dataPtr = reinterpret_cast< unsigned char * >( buffer );

  if (this->m_Compression == 0x00b1)
  {
    // Unpack binary data, each byte becomes a 2x2x2 block of voxels
    size_t xinc = (xsize+1)/2;
    size_t yinc = (ysize+1)/2;
    unsigned char v = input[size-1];
    v = (v == 0 ? 0x7f : v);
    unsigned char bit = 0;
    for (int i = 0; i < zsize; i++)
    {
      bit ^= (bit & 2);
      for (int j = 0; j < ysize; j++)
      {
        char *inPtr = input + (i*yinc + j)*xinc;
        bit ^= (bit & 1);
        for (int k = 0; k < xsize; k++)
        {
          unsigned char c = *inPtr;
          *dataPtr++ = ((c >> bit) & 1)*v;
          inPtr += (bit & 1);
          bit ^= 1;
        }
        bit ^= 2;
      }
      bit ^= 4;
    }
  }
  else if (this->m_Compression == 0x00b2)
  {
    // Decompress binary run-lengths
    bool flip = 0;
    unsigned char v = input[flip];
    char *inPtr = input + 2;
    size -= 2;
    if (size > 0)
    {
      do
      {
        unsigned char l = *inPtr++;
        if (l == 255)
        {
          l = 254;
          flip = !flip;
        }
        if (l > outSize)
        {
          l = static_cast<unsigned char>(outSize);
        }
        outSize -= l;
        if (l > 0)
        {
          do
          {
            *dataPtr++ = v;
          }
          while (--l);
        }
        flip = !flip;
        v = input[flip];
      }
      while (--size != 0 && outSize != 0);
    }
  }
  else if (this->m_Compression == 0x00c2)
  {
    // Decompress 8-bit run-lengths
    char *inPtr = input;
    size /= 2;
    if (size > 0)
    {
      do
      {
        unsigned char l = inPtr[0];
        unsigned char v = inPtr[1];
        inPtr += 2;
        if (l > outSize)
        {
          l = static_cast<unsigned char>(outSize);
        }
        outSize -= l;
        if (l > 0)
        {
          do
          {
            *dataPtr++ = v;
          }
          while (--l);
        }
      }
      while (--size != 0 && outSize != 0);
    }
  }

  delete [] input;
}


bool
ScancoImageIO
::CanWriteFile(const char *name)
{
  const std::string filename = name;

  if (  filename == "" )
    {
    return false;
    }

  std::string filenameLower = filename;
  std::transform(filenameLower.begin(), filenameLower.end(), filenameLower.begin(), ::tolower);
  std::string::size_type isqPos = filenameLower.rfind(".isq");
  if ( ( isqPos != std::string::npos )
       && ( isqPos == filename.length() - 4 ) )
    {
    return true;
    }

  return false;
}


void
ScancoImageIO
::WriteISQHeader(std::ofstream *file)
{
  delete [] this->m_RawHeader;
  this->m_RawHeader = new char[512];
  char * header = this->m_RawHeader;

  ScancoImageIO::PadString( header, this->m_Version, 16 ); header += 16;
  // 3 -> ISQ data type
  ScancoImageIO::EncodeInt( 3, header ); header += 4;
  const SizeValueType numberOfBytes = static_cast< SizeValueType >( this->GetImageSizeInBytes() );
  if( numberOfBytes > NumericTraits< int >::max() )
    {
    ScancoImageIO::EncodeInt( 0, header ); header += 4;
    }
  else
    {
    ScancoImageIO::EncodeInt( numberOfBytes, header ); header += 4;
    }
  ScancoImageIO::EncodeInt( numberOfBytes / 512, header ); header += 4;
  ScancoImageIO::EncodeInt( this->m_PatientIndex, header ); header += 4;
  ScancoImageIO::EncodeInt( this->m_ScannerID, header ); header += 4;
  ScancoImageIO::EncodeDate( header ); header += 8;
  for( unsigned int dimension = 0; dimension < 3; ++dimension )
    {
    // pixdim
    ScancoImageIO::EncodeInt( this->GetDimensions( dimension ), header ); header += 4;
    }
  for( unsigned int dimension = 0; dimension < 3; ++dimension )
    {
    // physdim
    ScancoImageIO::EncodeInt( this->GetSpacing( dimension ) * this->GetDimensions( dimension ) * 1e3, header ); header += 4;
    }
  ScancoImageIO::EncodeInt( (int)(this->m_SliceThickness * 1e3 ), header ); header += 4;
  ScancoImageIO::EncodeInt( (int)(this->m_SliceIncrement * 1e3 ), header ); header += 4;
  ScancoImageIO::EncodeInt( (int)(this->m_StartPosition * 1e3 ), header ); header += 4;
  ScancoImageIO::EncodeInt( (int)(this->m_DataRange[0] ), header ); header += 4;
  ScancoImageIO::EncodeInt( (int)(this->m_DataRange[1] ), header ); header += 4;
  ScancoImageIO::EncodeInt( (int)(this->m_MuScaling ), header ); header += 4;
  ScancoImageIO::EncodeInt( this->m_NumberOfSamples, header ); header += 4;
  ScancoImageIO::EncodeInt( this->m_NumberOfProjections, header ); header += 4;
  ScancoImageIO::EncodeInt( (int)(this->m_ScanDistance * 1e3 ), header ); header += 4;
  ScancoImageIO::EncodeInt( (int)(this->m_ScannerType), header ); header += 4;
  ScancoImageIO::EncodeInt( (int)(this->m_SampleTime * 1e3 ), header ); header += 4;
  ScancoImageIO::EncodeInt( (int)(this->m_MeasurementIndex), header ); header += 4;
  ScancoImageIO::EncodeInt( (int)(this->m_Site), header ); header += 4;
  ScancoImageIO::EncodeInt( (int)(this->m_ReferenceLine * 1e3), header ); header += 4;
  ScancoImageIO::EncodeInt( (int)(this->m_ReconstructionAlg), header ); header += 4;
  ScancoImageIO::PadString( header, this->m_PatientName, 40 ); header += 40;
  ScancoImageIO::EncodeInt( (int)(this->m_Energy * 1e3 ), header ); header += 4;
  ScancoImageIO::EncodeInt( (int)(this->m_Intensity * 1e3 ), header ); header += 4;
  const std::size_t fillSize = 83 * 4;
  std::memset( header, 0x00, fillSize );
  header += fillSize;
  // dataOffset
  const int dataOffset = 0;
  ScancoImageIO::EncodeInt( dataOffset, header ); header += 4;

  this->m_HeaderSize = static_cast< SizeValueType >( dataOffset + 1 ) * 512;
  this->m_Compression = 0;

  file->write(this->m_RawHeader, 512);
}


void
ScancoImageIO
::WriteImageInformation()
{
  if( this->m_FileName.empty() )
    {
    itkExceptionMacro( "FileName has not been set." );
    }

  std::ofstream outFile;
  this->OpenFileForWriting( outFile, m_FileName );

  this->WriteISQHeader( &outFile );

  outFile.close();
}


void
ScancoImageIO
::Write(const void *buffer)
{
  this->WriteImageInformation();

  std::ofstream outFile;
  this->OpenFileForWriting( outFile, m_FileName, false );
  outFile.seekp( this->m_HeaderSize, std::ios::beg );

  const SizeValueType numberOfBytes      = static_cast< SizeValueType >( this->GetImageSizeInBytes() );
  const SizeValueType numberOfComponents = static_cast< SizeValueType >( this->GetImageSizeInComponents() );

  if ( this->GetComponentType() != IOComponentEnum::SHORT )
    {
    itkExceptionMacro( "ScancoImageIO only supports writing short files." )
    }

  if ( itk::ByteSwapper< short >::SystemIsBigEndian() )
    {
    char *tempmemory = new char[numberOfBytes];
    memcpy(tempmemory, buffer, numberOfBytes);
      {
      ByteSwapper< short >::SwapRangeFromSystemToBigEndian(
        reinterpret_cast< short * >( tempmemory ), numberOfComponents);
      }

    // Write the actual pixel data
    outFile.write(static_cast< const char * >( tempmemory ), numberOfBytes);
    delete[] tempmemory;
    }
  else
    {
    outFile.write(static_cast< const char * >( buffer ), numberOfBytes);
    }

  outFile.close();
}

} // end namespace itk
