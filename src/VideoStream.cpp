
/*
 *  VideoStream.cpp
 *  sfeMovie project
 *
 *  Copyright (C) 2010-2014 Lucas Soltic
 *  lucas.soltic@orange.fr
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

extern "C" {
	#include <libavformat/avformat.h>
	#include <libavcodec/avcodec.h>
	#include <libswscale/swscale.h>
	#include <libavutil/imgutils.h>
}

#include "VideoStream.hpp"
#include "Utilities.hpp"
#include "Log.hpp"

namespace sfe {
	VideoStream::VideoStream(AVStreamRef stream, DataSource& dataSource, Timer& timer) :
	Stream(stream, dataSource, timer),
	m_texture(),
	m_rawVideoFrame(NULL),
	m_rgbaVideoBuffer(),
	m_rgbaVideoLinesize(),
	m_lastDecodedTimestamp(sf::Time::Zero)
	{
		int err;
		
		for (int i = 0; i < 4;i++) {
			m_rgbaVideoBuffer[i] = NULL;
			m_rgbaVideoLinesize[i] = 0;
		}
		
		m_rawVideoFrame = av_frame_alloc();
		CHECK(m_rawVideoFrame, "VideoStream() - out of memory");
		
		// RGBA video buffer
		err = av_image_alloc(m_rgbaVideoBuffer, m_rgbaVideoLinesize,
							 m_codecCtx->width, m_codecCtx->height,
							 PIX_FMT_RGBA, 1);
		CHECK(err >= 0, "VideoStream() - av_image_alloc() error");
		
		// SFML video frame
		err = m_texture.create(m_codecCtx->width, m_codecCtx->height);
		CHECK(err, "VideoStream() - sf::Texture::create() error");
		
		initRescaler();
	}
	
	VideoStream::~VideoStream(void)
	{
		if (m_rawVideoFrame) {
			av_frame_free(&m_rawVideoFrame);
		}
		
		if (m_rgbaVideoBuffer[0]) {
			av_freep(&m_rgbaVideoBuffer[0]);
		}
		
		if (m_swsCtx) {
			sws_freeContext(m_swsCtx);
		}
	}
	
	MediaType VideoStream::getStreamKind(void) const
	{
		return MEDIA_TYPE_VIDEO;
	}
	
	sf::Texture& VideoStream::getVideoTexture(void)
	{
		return m_texture;
	}
	
	void VideoStream::updateTexture(void)
	{
		if (getSynchronizationGap() < sf::Time::Zero) {
//			sfeLogDebug("Late by " + s(-getSynchronizationGap().asMilliseconds()) + "ms");
			onGetData(m_texture);
		}
	}
	
	bool VideoStream::onGetData(sf::Texture& texture)
	{
		AVPacketRef packet = popEncodedData();
		bool gotFrame = false;
		bool goOn = true;
		
		while (!gotFrame && packet && goOn) {
			bool needsMoreDecoding = false;
			
			CHECK(packet != NULL, "inconsistency error");
			goOn = decodePacket(packet, m_rawVideoFrame, gotFrame, needsMoreDecoding);
			
			if (gotFrame) {
				rescale(m_rawVideoFrame, m_rgbaVideoBuffer, m_rgbaVideoLinesize);
				texture.update(m_rgbaVideoBuffer[0]);
			}
			
			if (needsMoreDecoding) {
				prependEncodedData(packet);
			} else {
				av_free_packet(packet);
				av_free(packet);
			}
			
			if (!gotFrame && goOn) {
				sfeLogDebug("no image in this packet, reading further");
				packet = popEncodedData();
			}
		}
		
		return goOn;
	}
	
	sf::Time VideoStream::getSynchronizationGap(void)
	{
		return  m_lastDecodedTimestamp - m_timer.getOffset();
	}
	
	bool VideoStream::decodePacket(AVPacketRef packet, AVFrameRef outputFrame, bool& gotFrame, bool& needsMoreDecoding)
	{
		int gotPicture = 0;
		needsMoreDecoding = false;
		
		int decodedLength = avcodec_decode_video2(m_codecCtx, outputFrame, &gotPicture, packet);
		gotFrame = (gotPicture != 0);
		
		if (decodedLength > 0) {
			if (decodedLength < packet->size) {
				needsMoreDecoding = true;
				packet->data += decodedLength;
				packet->size -= decodedLength;
			}
			
			if (gotFrame) {
				int64_t timestamp = av_frame_get_best_effort_timestamp(outputFrame);
				int64_t startTime = m_stream->start_time != AV_NOPTS_VALUE ? m_stream->start_time : 0;
				sf::Int64 ms = 1000 * (timestamp - startTime) * av_q2d(m_stream->time_base);
				m_lastDecodedTimestamp = sf::milliseconds(ms);
			}
			
			return true;
		} else {
			return false;
		}
	}
	
	void VideoStream::initRescaler(void)
	{
		/* create scaling context */
		m_swsCtx = sws_getCachedContext(NULL, m_codecCtx->width, m_codecCtx->height, m_codecCtx->pix_fmt,
										m_codecCtx->width, m_codecCtx->height, PIX_FMT_RGBA,
										SWS_BILINEAR, NULL, NULL, NULL);
		CHECK(m_swsCtx, "VideoStream::initRescaler() - sws_getContext() error");
	}
	
	void VideoStream::rescale(AVFrameRef frame, uint8_t* outVideoBuffer[4], int outVideoLinesize[4])
	{
		CHECK(frame, "VideoStream::rescale() - invalid argument");
		sws_scale(m_swsCtx, frame->data, frame->linesize, 0, frame->height, outVideoBuffer, outVideoLinesize);
	}
	
	void VideoStream::preload(void)
	{
		onGetData(m_texture);
	}
	
	void VideoStream::willPlay(const Timer &timer)
	{
		preload();
	}
	
	void VideoStream::didPlay(const Timer& timer, Timer::Status previousStatus)
	{
		
	}
	
	void VideoStream::didPause(const Timer& timer, Timer::Status previousStatus)
	{
		
	}
	
	void VideoStream::didStop(const Timer& timer, Timer::Status previousStatus)
	{
		avcodec_flush_buffers(m_codecCtx);
	}
}
