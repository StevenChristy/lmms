/*
 * AudioFileOgg.cpp - audio-device which encodes wave-stream and writes it
 *                    into an OGG-file. This is used for song-export.
 *
 * This file is based on encode.c from vorbis-tools-source, for more information
 * see below.
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "AudioFileOgg.h"

#ifdef LMMS_HAVE_OGGVORBIS


#include <string>
#include <vector>
#include <vorbis/vorbisenc.h>

#include "Mixer.h"


AudioFileOgg::AudioFileOgg(	OutputSettings const & outputSettings,
				const ch_cnt_t channels,
				bool & successful,
				const QString & file,
				Mixer* mixer ) :
	AudioFileDevice( outputSettings, channels, file, mixer )
{
	m_ok = successful = outputFileOpened() && startEncoding();
}




AudioFileOgg::~AudioFileOgg()
{
	finishEncoding();
}




inline int AudioFileOgg::writePage()
{
	int written = writeData( m_og.header, m_og.header_len );
	written += writeData( m_og.body, m_og.body_len );
	return written;
}




bool AudioFileOgg::startEncoding()
{
	const char * cool_comment = "Cool=This song has been made using LMMS";
	const int max_comments = 10;
	const char *user_comments[max_comments];
	int user_comments_lengths[max_comments];
	std::vector<std::string> comments;

	comments.push_back(cool_comment);
	if ( getOutputSettings().getTitle().length() )
		comments.push_back( std::string("TITLE=") + std::string(getOutputSettings().getTitle().toLocal8Bit()) );
	if ( getOutputSettings().getArtist().length() )
		comments.push_back( std::string("ARTIST=") + std::string(getOutputSettings().getArtist().toLocal8Bit()) );
	if ( getOutputSettings().getAlbum().length() )
		comments.push_back( std::string("ALBUM=") + std::string(getOutputSettings().getAlbum().toLocal8Bit()) );
	if ( getOutputSettings().getGenre().length() )
		comments.push_back( std::string("GENRE=") + std::string(getOutputSettings().getGenre().toLocal8Bit()) );
	if ( getOutputSettings().getYear().length() )
		comments.push_back( std::string("YEAR=") + std::string(getOutputSettings().getYear().toLocal8Bit()) );

	if ( comments.size() > max_comments ) comments.resize(max_comments);

	for ( int x = 0; x < comments.size(); x++ ) 
	{
		user_comments[x] = comments[x].c_str();
		user_comments_lengths[x] = comments[x].size();
	}

	vorbis_comment vc;
	vc.user_comments = const_cast<char**>(&user_comments[0]);
	vc.comment_lengths = &user_comments_lengths[0];
	vc.comments = comments.size();
	vc.vendor = NULL;

	m_channels = channels();

	bool useVariableBitRate = getOutputSettings().getBitRateSettings().isVariableBitRate();
	bitrate_t minimalBitrate = nominalBitrate();
	bitrate_t maximumBitrate = nominalBitrate();

	if( useVariableBitRate )
	{
		minimalBitrate = minBitrate();		// min for vbr
		maximumBitrate = maxBitrate();		// max for vbr
	}


	m_rate 		= sampleRate();		// default-samplerate
	if( m_rate > 48000 )
	{
		m_rate = 48000;
		setSampleRate( 48000 );
	}

	m_comments 	= &vc;			// comments for ogg-file

	// Have vorbisenc choose a mode for us
	vorbis_info_init( &m_vi );

	if( vorbis_encode_setup_managed( &m_vi, m_channels, m_rate,
			( maximumBitrate > 0 )? maximumBitrate * 1000 : -1,
						nominalBitrate() * 1000, 
			( minimalBitrate > 0 )? minimalBitrate * 1000 : -1 ) )
	{
		printf( "Mode initialization failed: invalid parameters for "
								"bitrate\n" );
		vorbis_info_clear( &m_vi );
		return false;
	}

	if( useVariableBitRate )
	{
		// Turn off management entirely (if it was turned on).
		vorbis_encode_ctl( &m_vi, OV_ECTL_RATEMANAGE_SET, NULL );
	}
	else
	{
		vorbis_encode_ctl( &m_vi, OV_ECTL_RATEMANAGE_AVG, NULL );
	}

	vorbis_encode_setup_init( &m_vi );

	// Now, set up the analysis engine, stream encoder, and other
	// preparation before the encoding begins.
	vorbis_analysis_init( &m_vd, &m_vi );
	vorbis_block_init( &m_vd, &m_vb );

	// We give our ogg file a random serial number and avoid
	// 0 and UINT32_MAX which can get you into trouble.
	qsrand( time( 0 ) );
	m_serialNo = 0xD0000000 + qrand() % 0x0FFFFFFF;
	ogg_stream_init( &m_os, m_serialNo );

	// Now, build the three header packets and send through to the stream
	// output stage (but defer actual file output until the main encode
	// loop)

	ogg_packet header_main;
	ogg_packet header_comments;
	ogg_packet header_codebooks;
	int result;

	// Build the packets
	vorbis_analysis_headerout( &m_vd, m_comments, &header_main,
					&header_comments, &header_codebooks );

	// And stream them out
	ogg_stream_packetin( &m_os, &header_main );
	ogg_stream_packetin( &m_os, &header_comments );
	ogg_stream_packetin( &m_os, &header_codebooks );

	while( ( result = ogg_stream_flush( &m_os, &m_og ) ) )
	{
		if( !result )
		{
			break;
		}
		int ret = writePage();
		if( ret != m_og.header_len + m_og.body_len )
		{
			// clean up
			finishEncoding();
			return false;
		}
	}

	return true;
}




void AudioFileOgg::writeBuffer( const surroundSampleFrame * _ab,
						const fpp_t _frames,
						const float _master_gain )
{
	int eos = 0;

	float * * buffer = vorbis_analysis_buffer( &m_vd, _frames *
							BYTES_PER_SAMPLE *
								channels() );
	for( fpp_t frame = 0; frame < _frames; ++frame )
	{
		for( ch_cnt_t chnl = 0; chnl < channels(); ++chnl )
		{
			buffer[chnl][frame] = _ab[frame][chnl] * _master_gain;
		}
	}

	vorbis_analysis_wrote( &m_vd, _frames );

	// While we can get enough data from the library to analyse,
	// one block at a time...
	while( vorbis_analysis_blockout( &m_vd, &m_vb ) == 1 )
	{
		// Do the main analysis, creating a packet
		vorbis_analysis( &m_vb, NULL );
		vorbis_bitrate_addblock( &m_vb );

		while( vorbis_bitrate_flushpacket( &m_vd, &m_op ) )
		{
			// Add packet to bitstream
			ogg_stream_packetin( &m_os, &m_op );

			// If we've gone over a page boundary, we can do
			// actual output, so do so (for however many pages
			// are available)
			while( !eos )
			{
				int result = ogg_stream_pageout( &m_os,
								&m_og );
				if( !result )
				{
					break;
				}

				int ret = writePage();
				if( ret != m_og.header_len +
							m_og.body_len )
				{
					printf( "failed writing to "
								"outstream\n" );
					return;
				}
	
				if( ogg_page_eos( &m_og ) )
				{
					eos = 1;
				}
			}
		}
	}
}




void AudioFileOgg::finishEncoding()
{
	if( m_ok )
	{
		// just for flushing buffers...
		writeBuffer( NULL, 0, 0.0f );

		// clean up
		ogg_stream_clear( &m_os );

		vorbis_block_clear( &m_vb );
		vorbis_dsp_clear( &m_vd );
		vorbis_info_clear( &m_vi );
	}
}


#endif


