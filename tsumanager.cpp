#include "tsumanager.h"

int tsuManager::outstanding_resume_data = 0;

tsuManager::tsuManager()
{
    // setting default tsunami folder
    QString localTsunami = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation); // win -> C:\Users\user\AppData\Local\Tsunami
    localTsunami = QString("%0/%1").arg(localTsunami).arg("session");
    p_tsunamiSessionFolder = QDir::toNativeSeparators(localTsunami);

    // loading libtorrent stats metric indexes
    QHash<QString, int> statsList;
    std::vector<libtorrent::stats_metric> ssm = libtorrent::session_stats_metrics();
    for(libtorrent::stats_metric & metric : ssm) {
        statsList.insert(QString::fromUtf8(metric.name), metric.value_index);
    }
    qDebug() << QString("loaded %0 metric index from libtorrent").arg(QString::number(ssm.size()));

    // total number of bytes sent and received by the session (type 0 counter)
    // net.sent_payload_bytes     (included in net.sent_bytes)
    // net.sent_bytes
    // net.sent_ip_overhead_bytes (not included in net.sent_bytes)
    // net.sent_tracker_bytes     (not included in net.sent_bytes)
    
    // net.recv_payload_bytes
    // net.recv_bytes
    // net.recv_ip_overhead_bytes
    // net.recv_tracker_bytes
    
    // "net.recv_bytes" + "net.recv_ip_overhead_bytes" = total download

    p_net_recv_bytes = statsList["net.recv_bytes"];
    p_net_recv_payload_bytes = statsList["net.recv_payload_bytes"];
    p_net_recv_ip_overhead_bytes = statsList["net.recv_ip_overhead_bytes"];

    p_net_sent_bytes = statsList["net.sent_bytes"];
    p_net_sent_payload_bytes = statsList["net.sent_payload_bytes"];
    p_net_sent_ip_overhead_bytes = statsList["net.sent_ip_overhead_bytes"];

    p_ses_num_downloading_torrents = statsList["ses.num_downloading_torrents"];
    p_ses_num_queued_download_torrents = statsList["ses.num_queued_download_torrents"];
    p_ses_num_upload_only_torrents = statsList["ses.num_upload_only_torrents"];
    p_ses_num_seeding_torrents = statsList["ses.num_seeding_torrents"];
    p_ses_num_queued_seeding_torrents = statsList["ses.num_queued_seeding_torrents"];
    p_ses_num_checking_torrents = statsList["ses.num_checking_torrents"];
    p_ses_num_stopped_torrents = statsList["ses.num_stopped_torrents"];
    p_ses_num_error_torrents = statsList["ses.num_error_torrents"];

    p_timerUpdate = new QTimer(this);
    connect(p_timerUpdate, SIGNAL(timeout()), this, SLOT(postUpdates()));
    connect(this, SIGNAL(stopTimer()), p_timerUpdate, SLOT(stop()));
    connect(this, SIGNAL(finished()), p_timerUpdate, SLOT(deleteLater()));
}

void tsuManager::setNotify()
{
    p_session->set_alert_notify([this]()
    {
//        QTimer::singleShot(0, this, SLOT(alertsHandler()));
        QMetaObject::invokeMethod(this, "alertsHandler", Qt::AutoConnection);
    });
}

void tsuManager::loadSettings(libtorrent::settings_pack &settings)
{
//    libtorrent::settings_pack settings = p_session->get_settings();
    QSettings qtSettings(qApp->property("iniFilePath").toString(), QSettings::IniFormat);
    int downLimit = qtSettings.value("libtorrent/download_rate_limit", 0).toInt();
    int upLimit = qtSettings.value("libtorrent/upload_rate_limit", 0).toInt();

    // from KB/s to B/S
    downLimit = downLimit * 1000;
    upLimit = upLimit * 1000;

    QString user_agent = QString("Tsunami/%0.%1.%2").arg(VERSION_MAJOR).arg(VERSION_MINOR).arg(VERSION_BUGFIX);

    settings.set_str(libtorrent::settings_pack::user_agent, user_agent.toStdString());
    settings.set_int(libtorrent::settings_pack::download_rate_limit, downLimit); // B/S , 0 unlimited
    settings.set_int(libtorrent::settings_pack::upload_rate_limit, upLimit); // B/S , 0 unlimited

    // DEFAULTS from picotorrent (and qbittorrent)
    settings.set_str(libtorrent::settings_pack::string_types::dht_bootstrap_nodes,
        "router.bittorrent.com:6881" ","
        "router.utorrent.com:6881" ","
        "dht.transmissionbt.com:6881" ","
        "dht.aelitis.com:6881");
    settings.set_bool(libtorrent::settings_pack::upnp_ignore_nonrouters, true);
    settings.set_bool(libtorrent::settings_pack::announce_to_all_trackers, true);
    settings.set_bool(libtorrent::settings_pack::announce_to_all_tiers, true);
    settings.set_bool(libtorrent::settings_pack::enable_incoming_utp, true);
    settings.set_bool(libtorrent::settings_pack::enable_outgoing_utp, true);
    settings.set_bool(libtorrent::settings_pack::use_dht_as_fallback, false);
    settings.set_bool(libtorrent::settings_pack::anonymous_mode, false);
    settings.set_bool(libtorrent::settings_pack::dont_count_slow_torrents, false);
    settings.set_bool(libtorrent::settings_pack::rate_limit_ip_overhead, false);
    settings.set_bool(libtorrent::settings_pack::strict_super_seeding, false);
    settings.set_bool(libtorrent::settings_pack::no_connect_privileged_ports, false);
    settings.set_bool(libtorrent::settings_pack::apply_ip_filter_to_trackers, false);
    settings.set_int(libtorrent::settings_pack::stop_tracker_timeout, 1);
    settings.set_int(libtorrent::settings_pack::auto_scrape_interval, 1200);
    settings.set_int(libtorrent::settings_pack::auto_scrape_min_interval, 900);
    settings.set_int(libtorrent::settings_pack::cache_size, -1);
    settings.set_int(libtorrent::settings_pack::cache_expiry, 60);
    settings.set_int(libtorrent::settings_pack::disk_io_read_mode, 0);
    settings.set_int(libtorrent::settings_pack::active_seeds, 3);
    settings.set_int(libtorrent::settings_pack::active_downloads, 3);
    settings.set_int(libtorrent::settings_pack::active_limit, 5);
    settings.set_int(libtorrent::settings_pack::active_tracker_limit, -1);
    settings.set_int(libtorrent::settings_pack::active_dht_limit, -1);
    settings.set_int(libtorrent::settings_pack::active_lsd_limit, -1);
    settings.set_int(libtorrent::settings_pack::connections_limit, 500);
    settings.set_int(libtorrent::settings_pack::unchoke_slots_limit, -1);
    settings.set_int(libtorrent::settings_pack::mixed_mode_algorithm, 0);
    settings.set_int(libtorrent::settings_pack::connection_speed, 20);
    settings.set_int(libtorrent::settings_pack::seed_choking_algorithm, 1);

    int availThreads = ceil(QThread::idealThreadCount()/4);
    qDebug() << QString("found %0 ideal thread count, assigning %1 to hash threads").arg(QThread::idealThreadCount()).arg(availThreads);
    settings.set_int(libtorrent::settings_pack::aio_threads, availThreads);
//    p_session->apply_settings(settings);
}

void tsuManager::startManager()
{
    qDebug("starting");

    libtorrent::settings_pack settings;
    loadSettings(settings);
    p_session = QSharedPointer<libtorrent::session>::create(settings);
    setNotify();
    p_timerUpdate->start(1000);

    if (!QDir(p_tsunamiSessionFolder).exists()) {
        if (QDir().mkpath(p_tsunamiSessionFolder)) {
            qDebug() << "created" << p_tsunamiSessionFolder;
        } else {
            qWarning() << "cannot create" << p_tsunamiSessionFolder;
        }
    } else {
        // resuming
        qDebug() << "using" << p_tsunamiSessionFolder;

        // SESSIONSTATE
        QString sessionFileName = QString("%0/tsunami.session").arg(p_tsunamiSessionFolder);
        sessionFileName = QDir::toNativeSeparators(sessionFileName);

        QFileInfo sessionFileInfo(sessionFileName);
        qDebug() << QString("session file exists %0, is readable %1").arg((sessionFileInfo.exists()) ? "True" : "False")
                                                                     .arg((sessionFileInfo.isReadable()) ? "True" : "False");
        if (sessionFileInfo.exists() && sessionFileInfo.isReadable()) {
            QFile sessionFile(sessionFileName);
            sessionFile.open(QIODevice::ReadOnly);
            QByteArray sessionBuffer = sessionFile.readAll();
            libtorrent::error_code sec;
            libtorrent::bdecode_node session_state;
            libtorrent::bdecode(sessionBuffer.constData(), sessionBuffer.constData() + sessionBuffer.size(), session_state, sec);
            if (sec) {
                qCritical() << QString("session state load error: cannot decode %0, error %1").arg(sessionFileName)
                                                                                              .arg(QString::fromStdString(sec.message()));
            } else {
                p_session->load_state(session_state);
                qDebug("session state loaded");
            }
        } else {
            qWarning("cannot access session state file");
        }

        // FASTRESUMES
        int count = 0;
        QDirIterator it(p_tsunamiSessionFolder, QStringList() << "*.fastresume", QDir::Files, QDirIterator::Subdirectories);

        QSettings qtSettings(qApp->property("iniFilePath").toString(), QSettings::IniFormat);
        QStorageInfo storage = QStorageInfo::root();
        QString downloadPath = qtSettings.value("Download/downloadPath", storage.rootPath()).toString();

        while (it.hasNext()) {
            QString fileName = it.next();
//            qDebug() << QString("%0 exists %1").arg(fileName).arg(QFile::exists(fileName));

            QFile file(fileName);
            file.open(QIODevice::ReadOnly);
            QByteArray buf = file.readAll();
            libtorrent::error_code ec;
            libtorrent::bdecode_node bdn;
            libtorrent::bdecode(buf.constData(), buf.constData() + buf.size(), bdn, ec);
            if (ec) {
                qCritical() << QString("fastresume error: cannot decode %0").arg(fileName);
                continue;
            }
            if (bdn.type() != libtorrent::bdecode_node::type_t::dict_t) {
                qCritical() << QString("fastresume error: file %0 not a valid file").arg(fileName);
                continue;
            }

//            libtorrent::add_torrent_params tp = libtorrent::read_resume_data(bdn, ec);
//            if (ec) {
//                qCritical() << QString("fastresume error: cannot load fastresume %0").arg(fileName);
//                continue;
//            }
            std::ifstream ifs(fileName.toStdString(), std::ios_base::binary);
            ifs.unsetf(std::ios_base::skipws);

            libtorrent::add_torrent_params tp;
            tp.resume_data.assign(std::istream_iterator<char>(ifs), std::istream_iterator<char>());

            QString torrentName = fileName.replace("fastresume", "torrent");
            libtorrent::torrent_info ti(torrentName.toStdString());
//            tp.ti = std::make_shared<libtorrent::torrent_info>(ti);
            tp.ti = boost::make_shared<libtorrent::torrent_info>(ti);
//            tp.name = bdn.dict_find_string_value("zu-fileName").to_string();
            tp.name = bdn.dict_find_string_value("zu-fileName");
            tp.save_path = downloadPath.toStdString();
            p_session->async_add_torrent(tp);
            count++;
        }
        if (count == 0) {
            qDebug("no fastresumes to load");
        } else {
            qDebug() << QString("loaded %0 fastresumes").arg(count);
        }
    }
}

void tsuManager::stopManager()
{
    emit stopTimer();
    p_session->pause();

    // SAVE SESSION STATE
    libtorrent::entry entry;
    p_session->save_state(entry);

    QString sessionFileName = QString("%0/tsunami.session").arg(p_tsunamiSessionFolder);
    sessionFileName = QDir::toNativeSeparators(sessionFileName);

    std::ofstream sessionOut(sessionFileName.toStdString(), std::ios_base::binary);
    sessionOut.unsetf(std::ios_base::skipws);

    libtorrent::bencode(std::ostream_iterator<char>(sessionOut), entry);
    qDebug("session state saved");

    // FASTRESUMES
    std::vector<libtorrent::torrent_handle> handles = p_session->get_torrents();
    qDebug() << QString("stopManager: Handling %0 handlers").arg(handles.size());

    for (libtorrent::torrent_handle i : handles)
    {
        libtorrent::torrent_handle &h = i;
        if (!h.is_valid()) continue;
        libtorrent::torrent_status s = h.status();
        if (!s.has_metadata) continue;
        if (!s.need_save_resume) continue;
        h.save_resume_data();
        tsuManager::outstanding_resume_data++;
    }

    while (tsuManager::outstanding_resume_data > 0)
    {
//        qDebug() << QString("stopManager: outstanding_resume_data = %0").arg(outstanding_resume_data);
        libtorrent::alert const* a = p_session->wait_for_alert(libtorrent::seconds(10));

        // if we don't get an alert within 10 seconds, abort
        if (a == 0) continue;

        std::vector<libtorrent::alert*> alerts;
        p_session->pop_alerts(&alerts);

        if (alerts.size() == 0) continue;

        for (libtorrent::alert* a : alerts)
        {
            if (a == nullptr) continue;

//            qDebug() << QString("  %0:%1").arg(a->what()).arg(a->message().c_str());

            if (a == nullptr || a->type() == 0) continue;

            switch (a->type()) {
            case libtorrent::save_resume_data_failed_alert::alert_type:
                tsuManager::outstanding_resume_data--;
                break;
            case libtorrent::save_resume_data_alert::alert_type:
                if (a == nullptr) continue;
                libtorrent::save_resume_data_alert const* rd = libtorrent::alert_cast<libtorrent::save_resume_data_alert>(a);
                libtorrent::torrent_handle h = rd->handle;
                libtorrent::torrent_status st = h.status(libtorrent::torrent_handle::query_save_path | libtorrent::torrent_handle::query_name);

//                libtorrent::entry lte = libtorrent::write_resume_data(rd->params);
//                libtorrent::entry &en = lte;
//                en.dict().insert({ "zu-fileName", st.name });
                rd->resume_data->dict().insert({ "zu-fileName", st.name });

                std::stringstream hex;
                hex << st.info_hash;
                QString fileName = QString("%0/%1.fastresume").arg(p_tsunamiSessionFolder).arg(QString::fromStdString(hex.str()));
                fileName = QDir::toNativeSeparators(fileName);
                std::ofstream out(fileName.toStdString(), std::ios_base::binary);
                out.unsetf(std::ios_base::skipws);

//                libtorrent::bencode(std::ostream_iterator<char>(out), en);
                libtorrent::bencode(std::ostream_iterator<char>(out), *rd->resume_data);

                tsuManager::outstanding_resume_data--;
                break;
            }
        }
    }
    qDebug("fastresumes saved");

    qDebug("stopped, emitting finished");
    emit finished();
}

void tsuManager::alertsHandler()
{
    std::vector<libtorrent::alert*> alerts;
    p_session->pop_alerts(&alerts);
//    qDebug() << QString("processing %1 alerts:").arg(alerts.size());

    QVector<tsuEvents::tsuEvent> eventsArray;

    for (libtorrent::alert* alert : alerts)
    {
        if (alert == nullptr) continue;
        if (alert->type() != libtorrent::state_update_alert::alert_type && alert->type() != libtorrent::session_stats_alert::alert_type) {
            qDebug() << QString("%0::%1").arg(alert->what()).arg(alert->message().c_str());
        }

        switch (alert->type())
        {

        // ADD TORRENT
        case libtorrent::add_torrent_alert::alert_type:
        {
            libtorrent::add_torrent_alert* ata = libtorrent::alert_cast<libtorrent::add_torrent_alert>(alert);
            if (ata->error) {
                qDebug() << QString("tsuManager: error adding %0: %1")
                            .arg(alert->message().c_str())
                            .arg(ata->error.message().c_str());
            }
            if (ata->handle.torrent_file()) {
                // FROM .TORRENT
            } else {
                // FROM MAGNET
                qDebug() << "added from magnet";  // maybe we want to manage a freeze state until metadata_received_alert arrived
            }
            libtorrent::torrent_status const &ts = ata->handle.status();
            statusEnum se = static_cast<statusEnum>((int)ts.state);
            if (ts.paused) se = statusEnum::paused;
            tsuEvents::tsuEvent ev(ata->handle.info_hash().to_string(), ts.name.c_str(), ts.total_done,
                                   ts.total_upload, ts.download_rate, ts.upload_rate, ts.total_wanted,
                                   (int)se, ts.progress_ppm, ts.num_seeds, ts.num_peers);
            emit addFromSessionManager(ev);
            break;
        }

        // UPDATE TORRENT
        case libtorrent::state_update_alert::alert_type:
        {
            libtorrent::state_update_alert* sua = libtorrent::alert_cast<libtorrent::state_update_alert>(alert);
            if (sua->status.empty()) { break; }
            for (libtorrent::torrent_status const& s : sua->status)
            {
                statusEnum se = static_cast<statusEnum>((int)s.state);
                if (s.paused) se = statusEnum::paused;
                tsuEvents::tsuEvent ev(s.info_hash.to_string(), s.name.c_str(), s.total_done,
                                       s.total_upload, s.download_rate, s.upload_rate, s.total_wanted,
                                       (int)se, s.progress_ppm, s.num_seeds, s.num_peers);
                eventsArray.append(ev);
            }
            break;
        }

        // TORRENT DELETED
        case libtorrent::torrent_deleted_alert::alert_type:
        {
            libtorrent::torrent_deleted_alert* tra = libtorrent::alert_cast<libtorrent::torrent_deleted_alert>(alert);
            std::stringstream hex;
            hex << tra->info_hash;
            QString hash = QString::fromStdString(hex.str());
            QString fileName = QString("%0/%1.fastresume").arg(p_tsunamiSessionFolder).arg(hash);
            fileName = QDir::toNativeSeparators(fileName);
            if (QFile::exists(fileName)) {
                QFile file(fileName);
                file.remove();
            }
            fileName = QString("%0/%1.torrent").arg(p_tsunamiSessionFolder).arg(hash);
            fileName = QDir::toNativeSeparators(fileName);
            if (QFile::exists(fileName)) {
                QFile file(fileName);
                file.remove();
            }
//            emit torrentDeleted(tra->info_hash.to_string());
            break;
        }

        // EXTERNAL IP
        case libtorrent::external_ip_alert::alert_type:
        {
            libtorrent::external_ip_alert* eia = libtorrent::alert_cast<libtorrent::external_ip_alert>(alert);
            QString extIp = QString::fromStdString(eia->external_address.to_string());
            qDebug() << QString("received external ip %0").arg(extIp);
            emit externalIpAssigned();
            break;
        }

        // DHT BOOTSTRAP
        case libtorrent::dht_bootstrap_alert::alert_type:
        {
//            libtorrent::dht_bootstrap_alert* dba = libtorrent::alert_cast<libtorrent::dht_bootstrap_alert>(alert);
            qDebug("dht bootstrap done");
            emit dhtBootstrapExecuted();
            break;
        }

        // LISTEN SUCCEEDED
        case libtorrent::listen_succeeded_alert::alert_type:
        {
            libtorrent::listen_succeeded_alert* lsa = libtorrent::alert_cast<libtorrent::listen_succeeded_alert>(alert);
            QString type = "";
//            if (lsa->socket_type == libtorrent::socket_type_t::tcp) {
            if (lsa->sock_type == libtorrent::listen_succeeded_alert::socket_type_t::tcp) {
                type = "tcp";
//            } else if (lsa->socket_type == libtorrent::socket_type_t::udp) {
            } else if (lsa->sock_type == libtorrent::listen_succeeded_alert::socket_type_t::udp) {
                type = "udp";
            }
//            qDebug() << QString("listen succeeded for %0 on port %1").arg(type).arg(lsa->port);
            qDebug() << QString("listen succeeded for %0 on port %1").arg(type).arg(lsa->endpoint.port());
            emit listenerUpdate(type, true);
            break;
        }

        // LISTEN FAILED
        case libtorrent::listen_failed_alert::alert_type:
        {
            libtorrent::listen_failed_alert* lfa = libtorrent::alert_cast<libtorrent::listen_failed_alert>(alert);
            QString type = "";
//            if (lfa->socket_type == libtorrent::socket_type_t::tcp) {
            if (lfa->sock_type == libtorrent::listen_succeeded_alert::socket_type_t::tcp) {
                type = "tcp";
//            } else if (lfa->socket_type == libtorrent::socket_type_t::udp) {
            } else if (lfa->sock_type == libtorrent::listen_succeeded_alert::socket_type_t::udp) {
                type = "udp";
            }
//            qDebug() << QString("listen failed for %0 on port %1").arg(type).arg(lfa->port);
            qDebug() << QString("listen succeeded for %0 on port %1").arg(type).arg(lfa->endpoint.port());
            emit listenerUpdate(type, false);
            break;
        }

        case libtorrent::metadata_received_alert::alert_type:
        {
            libtorrent::metadata_received_alert *mra = libtorrent::alert_cast<libtorrent::metadata_received_alert>(alert);
            libtorrent::torrent_handle th = mra->handle;
            if (th.is_valid()) {

//                std::shared_ptr<libtorrent::torrent_info const> ti = th.torrent_file();
                boost::shared_ptr<libtorrent::torrent_info const> ti = th.torrent_file();

                libtorrent::create_torrent ct(*ti);
                libtorrent::entry te = ct.generate();
                std::vector<char> buffer;
                libtorrent::bencode(std::back_inserter(buffer), te);

                std::stringstream hex;
                hex << ti->info_hash();
                QString newFilePath = QDir::toNativeSeparators(QString("%0/%1.torrent").arg(p_tsunamiSessionFolder)
                                                               .arg(QString::fromStdString(hex.str())));
                FILE* f = fopen(newFilePath.toStdString().c_str(), "wb+");
                if (f) {
                    fwrite(&buffer[0], 1, buffer.size(), f);
                    fclose(f);
                }
                qDebug() << "torrent saved from metadata for" << mra->torrent_name();
            } else {
                qDebug() << "received invalid metadata for" << mra->torrent_name();
            }
            break;
        }

        // SESSION STATS
        case libtorrent::session_stats_alert::alert_type:
        {
            libtorrent::session_stats_alert *ssa = libtorrent::alert_cast<libtorrent::session_stats_alert>(alert);

            // "net.recv_bytes" + "net.recv_ip_overhead_bytes" = total download
            quint64 recvbytes = ssa->values[p_net_recv_bytes];
            quint64 recvbytesPayload = ssa->values[p_net_recv_payload_bytes]; // included in recv_bytes, subctracted for exact downloaded bytes count
            recvbytes -= recvbytesPayload;

            quint64 sentbytes = ssa->values[p_net_sent_bytes];
            quint64 sentbytesPayload = ssa->values[p_net_sent_payload_bytes];
            sentbytes -= sentbytesPayload;

            quint64 numDownloading = ssa->values[p_ses_num_downloading_torrents];
            quint64 numQueuedDown  = ssa->values[p_ses_num_queued_download_torrents];
            quint64 numUploading   = ssa->values[p_ses_num_upload_only_torrents];
            quint64 numSeeding     = ssa->values[p_ses_num_seeding_torrents];
            quint64 numQueuedSeed  = ssa->values[p_ses_num_queued_seeding_torrents];
            quint64 numChecking    = ssa->values[p_ses_num_checking_torrents];
            quint64 numStopped     = ssa->values[p_ses_num_stopped_torrents];
            quint64 numError       = ssa->values[p_ses_num_error_torrents];

            numDownloading += numQueuedDown;
            numUploading   += numSeeding + numQueuedSeed;

            emit sessionStatisticUpdate(sentbytes, recvbytes, numDownloading, numUploading, numChecking,
                                        numStopped, numError, numQueuedDown, numQueuedSeed);
            break;
        }
        default:
            break;
        }

        if (!eventsArray.isEmpty()) {
            emit updateFromSessionManager(eventsArray);
        }
    }
    p_session->post_torrent_updates(libtorrent::alert::status_notification | libtorrent::alert::progress_notification);
}

void tsuManager::postUpdates()
{
//    p_session->post_torrent_updates(libtorrent::alert::status_notification | libtorrent::alert::progress_notification);
      p_session->post_session_stats();
}

tsuManager::~tsuManager()
{
    qDebug("tsuManager destroyed");
//    delete timerUpdate;
//    p_session.clear();
}

void tsuManager::addItems(const QStringList && items, const QString &path)
{
    foreach (const QString &str, items)
    {
        qDebug() << "processing" << str;
        try
        {
            libtorrent::add_torrent_params atp;
            libtorrent::torrent_info ti(str.toStdString());

            if (!ti.metadata()) qWarning() << "No metadata for" << str;
            if (!ti.is_valid()) qWarning() << "torrent" << str << "is invalid";

//            atp.ti = std::make_shared<libtorrent::torrent_info>(ti);
            atp.ti = boost::make_shared<libtorrent::torrent_info>(ti);

            atp.save_path = path.toStdString();

            atp.flags &= ~libtorrent::add_torrent_params::flag_paused; // Start in pause
            atp.flags &= ~libtorrent::add_torrent_params::flag_auto_managed; // Because it is added in paused state
//            atp.flags &= ~libtorrent::add_torrent_params::flag_duplicate_is_error; // Already checked

            p_session->async_add_torrent(atp);

            std::stringstream hex;
            hex << ti.info_hash();
            QString newFilePath = QDir::toNativeSeparators(QString("%0/%1.torrent").arg(p_tsunamiSessionFolder)
                                                           .arg(QString::fromStdString(hex.str())));
            QFile::copy(str, newFilePath);

            qInfo() << QString("torrent %0 added").arg(str);
        }
        catch (std::exception &exc)
        {
            qCritical() << QString("addItems throws %0").arg(exc.what());
        }
    }
}

void tsuManager::addFromMagnet(const QStringList &&items, const QString &path)
{
    foreach (const QString &str, items)
    {
        qDebug() << "processing magnet" << str;
        try
        {
            libtorrent::error_code ec;
            libtorrent::add_torrent_params atp;
            atp.flags &= ~libtorrent::add_torrent_params::flag_paused; // Start in pause
            atp.flags &= ~libtorrent::add_torrent_params::flag_auto_managed; // Because it is added in paused state
            atp.save_path = path.toStdString();

            libtorrent::parse_magnet_uri(str.toStdString(), atp, ec);

            // MANAGE ERROR ON ec
            p_session->async_add_torrent(atp);

            qInfo() << QString("torrent %0 added").arg(str);
        }
        catch (std::exception &exc)
        {
            qCritical() << QString("addFromMagnet throws %0").arg(exc.what());
        }
    }
}

void tsuManager::getCancelRequest(const std::string &hash, const bool deleteFilesToo)
{
    try {
        libtorrent::sha1_hash sh(hash);
        libtorrent::torrent_handle th = p_session->find_torrent(sh);
        const libtorrent::torrent_handle &addTh = th;
        p_session->remove_torrent(addTh, (int)deleteFilesToo);
        emit torrentDeleted(hash);
    } catch (std::exception &exc) {
        qCritical() << QString("getCancelRequest throws %0").arg(exc.what());
//        emit torrentDeleted(hash);
    }
}

void tsuManager::getPauseRequest(const std::string &hash)
{
    try {
        libtorrent::sha1_hash sh(hash);
        libtorrent::torrent_handle th = p_session->find_torrent(sh);
        th.pause();
    } catch (std::exception &exc) {
        qCritical() << QString("getPauseRequest throws %0").arg(exc.what());
    }
}

void tsuManager::getResumeRequest(const std::string &hash)
{
    try {
        libtorrent::sha1_hash sh(hash);
        libtorrent::torrent_handle th = p_session->find_torrent(sh);
        th.resume();
    } catch (std::exception &exc) {
        qCritical() << QString("getResumeRequest throws %0").arg(exc.what());
    }
}

void tsuManager::refreshSettings()
{
    qDebug("received refreshSettings");
    libtorrent::settings_pack settings = p_session->get_settings();
    loadSettings(settings);
    p_session->apply_settings(settings);
}

