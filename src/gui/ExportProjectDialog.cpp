/*
 * ExportProjectDialog.cpp - implementation of dialog for exporting project
 *
 * Copyright (c) 2004-2013 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QDebug>

#include "ExportProjectDialog.h"
#include "Song.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "OutputSettings.h"


ExportProjectDialog::ExportProjectDialog( const QString & _file_name,
							QWidget * _parent, bool multi_export=false ) :
	QDialog( _parent ),
	Ui::ExportProjectDialog(),
	m_fileName( _file_name ),
	m_fileExtension(),
	m_multiExport( multi_export ),
	m_renderManager( nullptr )
{
	setupUi( this );
	setWindowTitle( tr( "Export project to %1" ).arg(
					QFileInfo( _file_name ).fileName() ) );

	// Get the extension of the chosen file.
	QStringList parts = _file_name.split( '.' );
	QString fileExt;
	if( parts.size() > 0 )
	{
		fileExt = "." + parts[parts.size()-1];
	}

	int cbIndex = 0;
	for( int i = 0; i < ProjectRenderer::NumFileFormats; ++i )
	{
		if( ProjectRenderer::fileEncodeDevices[i].isAvailable() )
		{
			// Get the extension of this format.
			QString renderExt = ProjectRenderer::fileEncodeDevices[i].m_extension;

			// Add to combo box.
			fileFormatCB->addItem( ProjectRenderer::tr(
				ProjectRenderer::fileEncodeDevices[i].m_description ),
				QVariant( ProjectRenderer::fileEncodeDevices[i].m_fileFormat ) // Format tag; later used for identification.
			);

			// If this is our extension, select it.
			if( QString::compare( renderExt, fileExt,
									Qt::CaseInsensitive ) == 0 )
			{
				fileFormatCB->setCurrentIndex( cbIndex );
			}

			cbIndex++;
		}
	}
	
	int const MAX_LEVEL=8;
	for(int i=0; i<=MAX_LEVEL; ++i)
	{
		QString info="";
		if ( i==0 ){ info = tr( "( Fastest - biggest )" ); }
		else if ( i==MAX_LEVEL ){ info = tr( "( Slowest - smallest )" ); }
		
		compLevelCB->addItem(
			QString::number(i)+" "+info,
			QVariant(i/static_cast<double>(MAX_LEVEL))
		);
	}
	compLevelCB->setCurrentIndex(MAX_LEVEL/2);
#ifndef LMMS_HAVE_SF_COMPLEVEL
	// Disable this widget; the setting would be ignored by the renderer.
	compressionWidget->setVisible(false);
#endif

	connect( startButton, SIGNAL( clicked() ),
			this, SLOT( startBtnClicked() ) );

	KeyStore &kvs = Engine::getSong()->m_keyValueStores.getStore("$$exportprojectsettings");
	int intvalue;
	QString strvalue;
	bool boolvalue;
	if ( kvs.getValue("loopCountSB.value", intvalue) && intvalue>=loopCountSB->minimum() && intvalue<=loopCountSB->maximum() )
		loopCountSB->setValue(intvalue);
	if ( kvs.getValue("checkBoxVariableBitRate.value", boolvalue) )
		checkBoxVariableBitRate->setChecked(boolvalue);
	if ( kvs.getValue("exportLoopCB.value", boolvalue) )
		exportLoopCB->setChecked(boolvalue);
	if ( kvs.getValue("renderMarkersCB.value", boolvalue) )
		renderMarkersCB->setChecked(boolvalue);
	if ( kvs.getValue( "interpolationCB.value", intvalue ) && intvalue >= 0 && intvalue < interpolationCB->count() )
		interpolationCB->setCurrentIndex(intvalue);
	if ( kvs.getValue( "oversamplingCB.value", intvalue ) && intvalue >= 0 && intvalue < oversamplingCB->count() )
		oversamplingCB->setCurrentIndex(intvalue);
	if ( kvs.getValue( "stereoModeComboBox.value", intvalue ) && intvalue >= 0 && intvalue < stereoModeComboBox->count() )
		stereoModeComboBox->setCurrentIndex(intvalue);
	if ( kvs.getValue( "bitrateCB.value", intvalue ) && intvalue >= 0 && intvalue < bitrateCB->count() )
		bitrateCB->setCurrentIndex(intvalue);
	if ( kvs.getValue( "samplerateCB.value", intvalue ) && intvalue >= 0 && intvalue < samplerateCB->count() )
		samplerateCB->setCurrentIndex(intvalue);  
#ifdef LMMS_HAVE_SF_COMPLEVEL
	if ( kvs.getValue( "compLevelCB.value", intvalue ) && intvalue >= 0 && intvalue < compLevelCB->count() )
		compLevelCB->setCurrentIndex(intvalue);  
#endif
	if ( kvs.getValue("titleTagLE.value", strvalue ) )
		titleTagLE->setText(strvalue);
	if ( kvs.getValue("artistTagLE.value", strvalue ) )
		artistTagLE->setText(strvalue);
	if ( kvs.getValue("albumTagLE.value", strvalue ) )
		albumTagLE->setText(strvalue);
	if ( kvs.getValue("genreTagLE.value", strvalue ) )
		genreTagLE->setText(strvalue);
	if ( kvs.getValue("yearTagLE.value", strvalue ) )
		yearTagLE->setText(strvalue);
}


void ExportProjectDialog::reject()
{
	if( m_renderManager ) {
		m_renderManager->abortProcessing();
	}
	m_renderManager.reset(nullptr);

	QDialog::reject();
}



void ExportProjectDialog::accept()
{
	m_renderManager.reset(nullptr);
	QDialog::accept();

	gui->mainWindow()->resetWindowTitle();
}




void ExportProjectDialog::closeEvent( QCloseEvent * _ce )
{
	Engine::getSong()->setLoopRenderCount(1);
	if( m_renderManager ) {
		m_renderManager->abortProcessing();
	}

	QDialog::closeEvent( _ce );
}


OutputSettings::StereoMode mapToStereoMode(int index)
{
	switch (index)
	{
	case 0:
		return OutputSettings::StereoMode_Stereo;
	case 1:
		return OutputSettings::StereoMode_JointStereo;
	case 2:
		return OutputSettings::StereoMode_Mono;
	default:
		return OutputSettings::StereoMode_Stereo;
	}
}

void ExportProjectDialog::startExport()
{
	Mixer::qualitySettings qs =
			Mixer::qualitySettings(
					static_cast<Mixer::qualitySettings::Interpolation>(interpolationCB->currentIndex()),
					static_cast<Mixer::qualitySettings::Oversampling>(oversamplingCB->currentIndex()) );

	const int samplerates[5] = { 44100, 48000, 88200, 96000, 192000 };
	const bitrate_t bitrates[6] = { 64, 128, 160, 192, 256, 320 };

	bool useVariableBitRate = checkBoxVariableBitRate->isChecked();

	OutputSettings::BitRateSettings bitRateSettings(bitrates[ bitrateCB->currentIndex() ], useVariableBitRate);
	OutputSettings os = OutputSettings(
			samplerates[ samplerateCB->currentIndex() ],
			bitRateSettings,
			static_cast<OutputSettings::BitDepth>( depthCB->currentIndex() ),
			mapToStereoMode(stereoModeComboBox->currentIndex()) );
	os.setTitle(titleTagLE->text());
	os.setArtist(artistTagLE->text());
	os.setAlbum(albumTagLE->text());
	os.setGenre(genreTagLE->text());
	os.setYear(yearTagLE->text());

	if (compressionWidget->isVisible())
	{
		double level = compLevelCB->itemData(compLevelCB->currentIndex()).toDouble();
		os.setCompressionLevel(level);
	}

	// Make sure we have the the correct file extension
	// so there's no confusion about the codec in use.
	auto output_name = m_fileName;
	if (!(m_multiExport || output_name.endsWith(m_fileExtension,Qt::CaseInsensitive)))
	{
		output_name+=m_fileExtension;
	}
	m_renderManager.reset(new RenderManager( qs, os, m_ft, output_name ));

	Engine::getSong()->setExportLoop( exportLoopCB->isChecked() );
	Engine::getSong()->setRenderBetweenMarkers( renderMarkersCB->isChecked() );
	Engine::getSong()->setLoopRenderCount(loopCountSB->value());

	connect( m_renderManager.get(), SIGNAL( progressChanged( int ) ),
			progressBar, SLOT( setValue( int ) ) );
	connect( m_renderManager.get(), SIGNAL( progressChanged( int ) ),
			this, SLOT( updateTitleBar( int ) ));
	connect( m_renderManager.get(), SIGNAL( finished() ),
			this, SLOT( accept() ) ) ;
	connect( m_renderManager.get(), SIGNAL( finished() ),
			gui->mainWindow(), SLOT( resetWindowTitle() ) );

	if ( m_multiExport )
	{
		m_renderManager->renderTracks();
	}
	else
	{
		m_renderManager->renderProject();
	}
}


void ExportProjectDialog::onFileFormatChanged(int index)
{
	// Extract the format tag from the currently selected item,
	// and adjust the UI properly.
	QVariant format_tag = fileFormatCB->itemData(index);
	bool successful_conversion = false;
	auto exportFormat = static_cast<ProjectRenderer::ExportFileFormats>(
		format_tag.toInt(&successful_conversion)
	);
	Q_ASSERT(successful_conversion);

	bool stereoModeVisible = (exportFormat == ProjectRenderer::MP3File);

	bool sampleRateControlsVisible = (exportFormat != ProjectRenderer::MP3File);

	bool bitRateControlsEnabled =
			(exportFormat == ProjectRenderer::OggFile ||
			 exportFormat == ProjectRenderer::MP3File);

	bool bitDepthControlEnabled =
			(exportFormat == ProjectRenderer::WaveFile ||
			 exportFormat == ProjectRenderer::FlacFile);

	bool variableBitrateVisible = !(exportFormat == ProjectRenderer::MP3File || exportFormat == ProjectRenderer::FlacFile);

#ifdef LMMS_HAVE_SF_COMPLEVEL
	bool compressionLevelVisible = (exportFormat == ProjectRenderer::FlacFile);
	compressionWidget->setVisible(compressionLevelVisible);
#endif

	stereoModeWidget->setVisible(stereoModeVisible);
	sampleRateWidget->setVisible(sampleRateControlsVisible);

	bitrateWidget->setVisible(bitRateControlsEnabled);
	checkBoxVariableBitRate->setVisible(variableBitrateVisible);

	depthWidget->setVisible(bitDepthControlEnabled);
}

void ExportProjectDialog::startBtnClicked()
{
	auto &kvs = Engine::getSong()->m_keyValueStores.getStore( "$$exportprojectsettings" );
	kvs.setValue( "loopCountSB.value", loopCountSB->value() );
	kvs.setValue( "exportLoopCB.value", exportLoopCB->isChecked() );
	kvs.setValue( "renderMarkersCB.value", renderMarkersCB->isChecked() );
	kvs.setValue( "checkBoxVariableBitRate.value", checkBoxVariableBitRate->isChecked() );
	kvs.setValue( "interpolationCB.value", interpolationCB->currentIndex() );
	kvs.setValue( "oversamplingCB.value", oversamplingCB->currentIndex() );
	kvs.setValue( "stereoModeComboBox.value", stereoModeComboBox->currentIndex() );
	kvs.setValue( "bitrateCB.value", bitrateCB->currentIndex() );
	kvs.setValue( "samplerateCB.value", samplerateCB->currentIndex() );
#ifdef LMMS_HAVE_SF_COMPLEVEL
	kvs.setValue( "compLevelCB.value", compLevelCB->currentIndex() );
#endif
	kvs.setValue( "titleTagLE.value", titleTagLE->text());
	kvs.setValue( "artistTagLE.value", artistTagLE->text());
	kvs.setValue( "albumTagLE.value", albumTagLE->text());
	kvs.setValue( "genreTagLE.value", genreTagLE->text());
	kvs.setValue( "yearTagLE.value", yearTagLE->text());

	m_ft = ProjectRenderer::NumFileFormats;

	// Get file format from current menu selection.
	bool successful_conversion = false;
	QVariant tag = fileFormatCB->itemData(fileFormatCB->currentIndex());
	m_ft = static_cast<ProjectRenderer::ExportFileFormats>(
			tag.toInt(&successful_conversion)
	);

	if( !successful_conversion )
	{
		QMessageBox::information( this, tr( "Error" ),
								  tr( "Error while determining file-encoder device. "
									  "Please try to choose a different output "
									  "format." ) );
		reject();
		return;
	}

	// Find proper file extension.
	for( int i = 0; i < ProjectRenderer::NumFileFormats; ++i )
	{
		if (m_ft == ProjectRenderer::fileEncodeDevices[i].m_fileFormat)
		{
			m_fileExtension = QString( QLatin1String( ProjectRenderer::fileEncodeDevices[i].m_extension ) );
			break;
		}
	}

	startButton->setEnabled( false );
	progressBar->setEnabled( true );

	updateTitleBar( 0 );

	startExport();
}




void ExportProjectDialog::updateTitleBar( int _prog )
{
	gui->mainWindow()->setWindowTitle(
					tr( "Rendering: %1%" ).arg( _prog ) );
}
