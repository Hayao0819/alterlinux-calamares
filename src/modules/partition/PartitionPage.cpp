/* === This file is part of Calamares - <http://github.com/calamares> ===
 *
 *   Copyright 2014, Aurélien Gâteau <agateau@kde.org>
 *
 *   Calamares is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Calamares is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Calamares. If not, see <http://www.gnu.org/licenses/>.
 */

#include "PartitionPage.h"

// Local
#include <CreatePartitionDialog.h>
#include <DeviceModel.h>
#include <PartitionCoreModule.h>
#include <PartitionModel.h>
#include <PMUtils.h>
#include <ui_PartitionPage.h>
#include <ui_CreatePartitionTableDialog.h>

// CalaPM
#include <core/device.h>
#include <core/partition.h>

// Qt
#include <QDebug>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QPointer>

PartitionPage::PartitionPage( PartitionCoreModule* core, QWidget* parent )
    : QWidget( parent )
    , m_ui( new Ui_PartitionPage )
    , m_core( core )
{
    m_ui->setupUi( this );
    m_ui->deviceListView->setModel( m_core->deviceModel() );
    updateButtons();

    connect( m_ui->deviceListView->selectionModel(), &QItemSelectionModel::currentChanged,
             [ this ]( const QModelIndex& index, const QModelIndex& oldIndex )
    {
        Device* device = m_core->deviceModel()->deviceForIndex( index );
        PartitionModel* model = m_core->partitionModelForDevice( device );
        m_ui->partitionTreeView->setModel( model );

        // Must be done here because we need to have a model set to define
        // individual column resize mode
        QHeaderView* header = m_ui->partitionTreeView->header();
        header->setSectionResizeMode( QHeaderView::ResizeToContents );
        header->setSectionResizeMode( 0, QHeaderView::Stretch );

        updateButtons();
        // Establish connection here because selection model is destroyed when
        // model changes
        connect( m_ui->partitionTreeView->selectionModel(), &QItemSelectionModel::currentChanged,
                 [ this ]( const QModelIndex& index, const QModelIndex& oldIndex )
        {
            updateButtons();
        } );
        connect( model, &QAbstractItemModel::modelReset, this, &PartitionPage::updateButtons );
    } );

    connect( m_ui->newPartitionTableButton, &QAbstractButton::clicked, this, &PartitionPage::onNewPartitionTableClicked );
    connect( m_ui->createButton, &QAbstractButton::clicked, this, &PartitionPage::onCreateClicked );
    connect( m_ui->editButton, &QAbstractButton::clicked, this, &PartitionPage::onEditClicked );
    connect( m_ui->deleteButton, &QAbstractButton::clicked, this, &PartitionPage::onDeleteClicked );
}

PartitionPage::~PartitionPage()
{
}

void
PartitionPage::updateButtons()
{
    bool create = false, edit = false, del = false;

    QModelIndex index = m_ui->partitionTreeView->currentIndex();
    if ( index.isValid() )
    {
        const PartitionModel* model = static_cast< const PartitionModel* >( index.model() );
        Q_ASSERT( model );
        Partition* partition = model->partitionForIndex( index );
        Q_ASSERT( partition );
        bool isFree = PMUtils::isPartitionFreeSpace( partition );
        create = isFree;
        edit = del = !isFree;
    }
    m_ui->createButton->setEnabled( create );
    m_ui->editButton->setEnabled( edit );
    m_ui->deleteButton->setEnabled( del );

    m_ui->newPartitionTableButton->setEnabled( m_ui->deviceListView->currentIndex().isValid() );
}

void
PartitionPage::onNewPartitionTableClicked()
{
    QModelIndex index = m_ui->deviceListView->currentIndex();
    Q_ASSERT( index.isValid() );
    Device* device = m_core->deviceModel()->deviceForIndex( index );

    QPointer<QDialog> dlg = new QDialog( this );
    Ui_CreatePartitionTableDialog ui;
    ui.setupUi( dlg.data() );
    QString areYouSure = tr( "Are you sure you want to create a new partition table on %1?" ).arg( device->name() );
    ui.areYouSureLabel->setText( areYouSure );
    if ( dlg->exec() == QDialog::Accepted )
    {
        PartitionTable::TableType type = ui.mbrRadioButton->isChecked() ? PartitionTable::msdos : PartitionTable::gpt;
        m_core->createPartitionTable( device, type );
    }
    delete dlg;
}

void
PartitionPage::onCreateClicked()
{
    QModelIndex index = m_ui->partitionTreeView->currentIndex();
    Q_ASSERT( index.isValid() );

    const PartitionModel* model = static_cast< const PartitionModel* >( index.model() );
    Partition* partition = model->partitionForIndex( index );
    Q_ASSERT( partition );

    QPointer<CreatePartitionDialog> dlg = new CreatePartitionDialog( model->device(), partition->parent(), this );
    dlg->initFromFreeSpace( partition );
    if ( dlg->exec() == QDialog::Accepted )
        m_core->createPartition( model->device(), dlg->createPartitionInfo() );
    delete dlg;
}

void
PartitionPage::onEditClicked()
{
    QModelIndex index = m_ui->partitionTreeView->currentIndex();
    Q_ASSERT( index.isValid() );

    const PartitionModel* model = static_cast< const PartitionModel* >( index.model() );
    Partition* partition = model->partitionForIndex( index );
    PartitionInfo* partitionInfo = model->partitionInfoForIndex( index );
    Q_ASSERT( partition );

    if ( PMUtils::isPartitionNew( partitionInfo->partition ) )
        updatePartitionToCreate( model->device(), partitionInfo );
    else
        editExistingPartition( partition );
}

void
PartitionPage::onDeleteClicked()
{
    QModelIndex index = m_ui->partitionTreeView->currentIndex();
    Q_ASSERT( index.isValid() );

    const PartitionModel* model = static_cast< const PartitionModel* >( index.model() );
    Partition* partition = model->partitionForIndex( index );
    Q_ASSERT( partition );

    m_core->deletePartition( model->device(), partition );
}

void
PartitionPage::updatePartitionToCreate( Device* device, PartitionInfo* partitionInfo )
{
    Partition* partition = partitionInfo->partition;
    QPointer<CreatePartitionDialog> dlg = new CreatePartitionDialog( device, partition->parent(), this );
    dlg->initFromPartitionInfo( partitionInfo );
    if ( dlg->exec() == QDialog::Accepted )
    {
        m_core->deletePartition( device, partition );
        m_core->createPartition( device, dlg->createPartitionInfo() );
    }
    delete dlg;
}

void
PartitionPage::editExistingPartition( Partition* partition )
{
}
