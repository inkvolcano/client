#include "RundownAtemKeyerStateWidget.h"

#include "Global.h"

#include "DatabaseManager.h"
#include "DeviceManager.h"
#include "AtemDeviceManager.h"
#include "GpiManager.h"
#include "EventManager.h"
#include "Events/ConnectionStateChangedEvent.h"
#include "Utils/ItemScheduler.h"

#include <QtCore/QObject>
#include <QtCore/QXmlStreamWriter>

#include <QtWidgets/QGraphicsOpacityEffect>

RundownAtemKeyerStateWidget::RundownAtemKeyerStateWidget(const LibraryModel& model, QWidget* parent, const QString& color, bool active,
                                                         bool inGroup, bool compactView)
    : QWidget(parent),
      active(active), inGroup(inGroup), compactView(compactView), color(color), model(model), playControlSubscription(NULL),
      playNowControlSubscription(NULL), updateControlSubscription(NULL)
{
    setupUi(this);

    this->animation = new ActiveAnimation(this->labelActiveColor);

    this->markUsedItems = (DatabaseManager::getInstance().getConfigurationByName("MarkUsedItems").getValue() == "true") ? true : false;

    setColor(color);
    setActive(active);
    setCompactView(compactView);

    this->labelGroupColor->setVisible(this->inGroup);
    this->labelGroupColor->setStyleSheet(QString("background-color: %1;").arg(Color::DEFAULT_GROUP_COLOR));
    this->labelColor->setStyleSheet(QString("background-color: %1;").arg(Color::DEFAULT_ATEM_COLOR));

    this->labelLabel->setText(this->model.getLabel());
    this->labelDelay->setText(QString("Delay: %1").arg(this->command.getDelay()));
    this->labelDevice->setText(QString("%1").arg(this->model.getDeviceName()));

    QObject::connect(&this->itemScheduler, SIGNAL(executePlay()), this, SLOT(executePlay()));

    QObject::connect(&this->command, SIGNAL(delayChanged(int)), this, SLOT(delayChanged(int)));
    QObject::connect(&this->command, SIGNAL(allowGpiChanged(bool)), this, SLOT(allowGpiChanged(bool)));
    QObject::connect(&this->command, SIGNAL(remoteTriggerIdChanged(const QString&)), this, SLOT(remoteTriggerIdChanged(const QString&)));
    QObject::connect(&EventManager::getInstance(), SIGNAL(preview(const PreviewEvent&)), this, SLOT(preview(const PreviewEvent&)));
    QObject::connect(&EventManager::getInstance(), SIGNAL(atemDeviceChanged(const AtemDeviceChangedEvent&)), this, SLOT(atemDeviceChanged(const AtemDeviceChangedEvent&)));
    QObject::connect(&EventManager::getInstance(), SIGNAL(labelChanged(const LabelChangedEvent&)), this, SLOT(labelChanged(const LabelChangedEvent&)));

    QObject::connect(&AtemDeviceManager::getInstance(), SIGNAL(deviceAdded(AtemDevice&)), this, SLOT(deviceAdded(AtemDevice&)));
    const QSharedPointer<AtemDevice> device = AtemDeviceManager::getInstance().getDeviceByName(this->model.getDeviceName());
    if (device != NULL)
        QObject::connect(device.data(), SIGNAL(connectionStateChanged(AtemDevice&)), this, SLOT(deviceConnectionStateChanged(AtemDevice&)));

    QObject::connect(GpiManager::getInstance().getGpiDevice().data(), SIGNAL(connectionStateChanged(bool, GpiDevice*)), this, SLOT(gpiConnectionStateChanged(bool, GpiDevice*)));

    checkEmptyDevice();
    checkGpiConnection();
    checkDeviceConnection();
}

void RundownAtemKeyerStateWidget::preview(const PreviewEvent& event)
{
    Q_UNUSED(event);

    // This event is not for us.
    if (!this->selected)
        return;

    executePlay();
}

void RundownAtemKeyerStateWidget::labelChanged(const LabelChangedEvent& event)
{
    // This event is not for us.
    if (!this->selected)
        return;

    this->model.setLabel(event.getLabel());

    this->labelLabel->setText(this->model.getLabel());
}

void RundownAtemKeyerStateWidget::atemDeviceChanged(const AtemDeviceChangedEvent& event)
{
    // This event is not for us.
    if (!this->selected)
        return;

    // Should we update the device name?
    if (!event.getDeviceName().isEmpty() && event.getDeviceName() != this->model.getDeviceName())
    {
        // Disconnect connectionStateChanged() from the old device.
        const QSharedPointer<AtemDevice> oldDevice = AtemDeviceManager::getInstance().getDeviceByName(this->model.getDeviceName());
        if (oldDevice != NULL)
            QObject::disconnect(oldDevice.data(), SIGNAL(connectionStateChanged(AtemDevice&)), this, SLOT(deviceConnectionStateChanged(AtemDevice&)));

        // Update the model with the new device.
        this->model.setDeviceName(event.getDeviceName());
        this->labelDevice->setText(QString("%1").arg(this->model.getDeviceName()));

        // Connect connectionStateChanged() to the new device.
        const QSharedPointer<AtemDevice> newDevice = AtemDeviceManager::getInstance().getDeviceByName(this->model.getDeviceName());
        if (newDevice != NULL)
            QObject::connect(newDevice.data(), SIGNAL(connectionStateChanged(AtemDevice&)), this, SLOT(deviceConnectionStateChanged(AtemDevice&)));
    }

    checkEmptyDevice();
    checkDeviceConnection();
}

AbstractRundownWidget* RundownAtemKeyerStateWidget::clone()
{
    RundownAtemKeyerStateWidget* widget = new RundownAtemKeyerStateWidget(this->model, this->parentWidget(), this->color, this->active,
                                                                          this->inGroup, this->compactView);

    AtemKeyerStateCommand* command = dynamic_cast<AtemKeyerStateCommand*>(widget->getCommand());
    command->setChannel(this->command.getChannel());
    command->setVideolayer(this->command.getVideolayer());
    command->setDelay(this->command.getDelay());
    command->setDuration(this->command.getDuration());
    command->setAllowGpi(this->command.getAllowGpi());
    command->setAllowRemoteTriggering(this->command.getAllowRemoteTriggering());
    command->setRemoteTriggerId(this->command.getRemoteTriggerId());
    command->setKeyer(this->command.getKeyer());
    command->setState(this->command.getState());
    command->setTriggerOnNext(this->command.getTriggerOnNext());
    command->setMixerStep(this->command.getMixerStep());

    return widget;
}

void RundownAtemKeyerStateWidget::setCompactView(bool compactView)
{
    if (compactView)
    {
        this->labelIcon->setFixedSize(Rundown::COMPACT_ICON_WIDTH, Rundown::COMPACT_ICON_HEIGHT);
        this->labelGpiConnected->setFixedSize(Rundown::COMPACT_ICON_WIDTH, Rundown::COMPACT_ICON_HEIGHT);
        this->labelDisconnected->setFixedSize(Rundown::COMPACT_ICON_WIDTH, Rundown::COMPACT_ICON_HEIGHT);
    }
    else
    {
        this->labelIcon->setFixedSize(Rundown::DEFAULT_ICON_WIDTH, Rundown::DEFAULT_ICON_HEIGHT);
        this->labelGpiConnected->setFixedSize(Rundown::DEFAULT_ICON_WIDTH, Rundown::DEFAULT_ICON_HEIGHT);
        this->labelDisconnected->setFixedSize(Rundown::DEFAULT_ICON_WIDTH, Rundown::DEFAULT_ICON_HEIGHT);
    }

    this->compactView = compactView;
}

void RundownAtemKeyerStateWidget::readProperties(boost::property_tree::wptree& pt)
{
    if (pt.count(L"color") > 0) setColor(QString::fromStdWString(pt.get<std::wstring>(L"color")));
}

void RundownAtemKeyerStateWidget::writeProperties(QXmlStreamWriter& writer)
{
    writer.writeTextElement("color", this->color);
}

bool RundownAtemKeyerStateWidget::isGroup() const
{
    return false;
}

bool RundownAtemKeyerStateWidget::isInGroup() const
{
    return this->inGroup;
}

AbstractCommand* RundownAtemKeyerStateWidget::getCommand()
{
    return &this->command;
}

LibraryModel* RundownAtemKeyerStateWidget::getLibraryModel()
{
    return &this->model;
}

void RundownAtemKeyerStateWidget::setSelected(bool selected)
{
    this->selected = selected;
}

void RundownAtemKeyerStateWidget::setActive(bool active)
{
    this->active = active;

    this->animation->stop();

    if (this->active)
        this->labelActiveColor->setStyleSheet(QString("background-color: %1;").arg(Color::DEFAULT_ACTIVE_COLOR));
    else
        this->labelActiveColor->setStyleSheet("");
}

void RundownAtemKeyerStateWidget::setInGroup(bool inGroup)
{
    this->inGroup = inGroup;
    this->labelGroupColor->setVisible(this->inGroup);
}

QString RundownAtemKeyerStateWidget::getColor() const
{
    return this->color;
}

void RundownAtemKeyerStateWidget::setColor(const QString& color)
{
    this->color = color;
    this->setStyleSheet(QString("#frameItem, #frameStatus { background-color: %1; }").arg(color));
}

void RundownAtemKeyerStateWidget::checkEmptyDevice()
{
    if (this->labelDevice->text() == "Device: ")
        this->labelDevice->setStyleSheet("color: firebrick;");
    else
        this->labelDevice->setStyleSheet("");
}

void RundownAtemKeyerStateWidget::clearDelayedCommands()
{
    this->itemScheduler.cancel();
}

void RundownAtemKeyerStateWidget::setUsed(bool used)
{
    if (used)
    {
        if (this->frameItem->graphicsEffect() == NULL)
        {
            QGraphicsOpacityEffect* effect = new QGraphicsOpacityEffect(this);
            effect->setOpacity(0.25);

            this->frameItem->setGraphicsEffect(effect);
        }
    }
    else
        this->frameItem->setGraphicsEffect(NULL);
}

bool RundownAtemKeyerStateWidget::executeCommand(Playout::PlayoutType type)
{
    if ((type == Playout::PlayoutType::Play && !this->command.getTriggerOnNext()) || type == Playout::PlayoutType::Update)
    {
        if (this->command.getDelay() < 0)
            return true;

        if (!this->model.getDeviceName().isEmpty()) // The user need to select a device.
        {
            this->itemScheduler.schedulePlayAndStop(this->command.getDelay(), 0, Output::DEFAULT_DELAY_IN_MILLISECONDS);
        }
    }
    else if (type == Playout::PlayoutType::PlayNow)
        executePlay();
    else if (type == Playout::PlayoutType::Next && this->command.getTriggerOnNext())
        executePlay();

    if (this->active)
        this->animation->start(1);

    return true;
}

void RundownAtemKeyerStateWidget::executePlay()
{
    const QSharedPointer<AtemDevice> device = AtemDeviceManager::getInstance().getDeviceByName(this->model.getDeviceName());
    if (device != NULL && device->isConnected())
        device->setKeyerState(this->command.getKeyer(), this->command.getState(), this->command.getMixerStep());

    if (this->markUsedItems)
        setUsed(true);
}

void RundownAtemKeyerStateWidget::delayChanged(int delay)
{
    this->labelDelay->setText(QString("Delay: %1").arg(delay));
}

void RundownAtemKeyerStateWidget::checkGpiConnection()
{
    this->labelGpiConnected->setVisible(this->command.getAllowGpi());

    if (GpiManager::getInstance().getGpiDevice()->isConnected())
        this->labelGpiConnected->setPixmap(QPixmap(":/Graphics/Images/GpiConnected.png"));
    else
        this->labelGpiConnected->setPixmap(QPixmap(":/Graphics/Images/GpiDisconnected.png"));
}

void RundownAtemKeyerStateWidget::checkDeviceConnection()
{
    const QSharedPointer<AtemDevice> device = AtemDeviceManager::getInstance().getDeviceByName(this->model.getDeviceName());
    if (device == NULL)
        this->labelDisconnected->setVisible(true);
    else
        this->labelDisconnected->setVisible(!device->isConnected());
}

void RundownAtemKeyerStateWidget::configureOscSubscriptions()
{
    if (!this->command.getAllowRemoteTriggering())
        return;

    if (this->playControlSubscription != NULL)
        this->playControlSubscription->disconnect(); // Disconnect all events.

    if (this->playNowControlSubscription != NULL)
        this->playNowControlSubscription->disconnect(); // Disconnect all events.

    if (this->updateControlSubscription != NULL)
        this->updateControlSubscription->disconnect(); // Disconnect all events.

    QString playControlFilter = Osc::ITEM_CONTROL_PLAY_FILTER;
    playControlFilter.replace("#UID#", this->command.getRemoteTriggerId());
    this->playControlSubscription = new OscSubscription(playControlFilter, this);
    QObject::connect(this->playControlSubscription, SIGNAL(subscriptionReceived(const QString&, const QList<QVariant>&)),
                     this, SLOT(playControlSubscriptionReceived(const QString&, const QList<QVariant>&)));

    QString playNowControlFilter = Osc::ITEM_CONTROL_PLAYNOW_FILTER;
    playNowControlFilter.replace("#UID#", this->command.getRemoteTriggerId());
    this->playNowControlSubscription = new OscSubscription(playNowControlFilter, this);
    QObject::connect(this->playNowControlSubscription, SIGNAL(subscriptionReceived(const QString&, const QList<QVariant>&)),
                     this, SLOT(playNowControlSubscriptionReceived(const QString&, const QList<QVariant>&)));

    QString updateControlFilter = Osc::ITEM_CONTROL_UPDATE_FILTER;
    updateControlFilter.replace("#UID#", this->command.getRemoteTriggerId());
    this->updateControlSubscription = new OscSubscription(updateControlFilter, this);
    QObject::connect(this->updateControlSubscription, SIGNAL(subscriptionReceived(const QString&, const QList<QVariant>&)),
                     this, SLOT(updateControlSubscriptionReceived(const QString&, const QList<QVariant>&)));
}

void RundownAtemKeyerStateWidget::allowGpiChanged(bool allowGpi)
{
    Q_UNUSED(allowGpi);

    checkGpiConnection();
}

void RundownAtemKeyerStateWidget::gpiConnectionStateChanged(bool connected, GpiDevice* device)
{
    Q_UNUSED(connected);
    Q_UNUSED(device);

    checkGpiConnection();
}

void RundownAtemKeyerStateWidget::remoteTriggerIdChanged(const QString& remoteTriggerId)
{
    configureOscSubscriptions();

    this->labelRemoteTriggerId->setText(QString("UID: %1").arg(remoteTriggerId));
}

void RundownAtemKeyerStateWidget::deviceConnectionStateChanged(AtemDevice& device)
{
    Q_UNUSED(device);

    checkDeviceConnection();
}

void RundownAtemKeyerStateWidget::deviceAdded(AtemDevice& device)
{
    if (AtemDeviceManager::getInstance().getDeviceModelByAddress(device.getAddress()).getName() == this->model.getDeviceName())
        QObject::connect(&device, SIGNAL(connectionStateChanged(AtemDevice&)), this, SLOT(deviceConnectionStateChanged(AtemDevice&)));

    checkDeviceConnection();
}

void RundownAtemKeyerStateWidget::playControlSubscriptionReceived(const QString& predicate, const QList<QVariant>& arguments)
{
    Q_UNUSED(predicate);

    if (this->command.getAllowRemoteTriggering() && arguments.count() > 0 && arguments[0].toInt() > 0)
        executeCommand(Playout::PlayoutType::Play);
}

void RundownAtemKeyerStateWidget::playNowControlSubscriptionReceived(const QString& predicate, const QList<QVariant>& arguments)
{
    Q_UNUSED(predicate);

    if (this->command.getAllowRemoteTriggering() && arguments.count() > 0 && arguments[0].toInt() > 0)
        executeCommand(Playout::PlayoutType::PlayNow);
}

void RundownAtemKeyerStateWidget::updateControlSubscriptionReceived(const QString& predicate, const QList<QVariant>& arguments)
{
    Q_UNUSED(predicate);

    if (this->command.getAllowRemoteTriggering() && arguments.count() > 0 && arguments[0].toInt() > 0)
        executeCommand(Playout::PlayoutType::Update);
}
