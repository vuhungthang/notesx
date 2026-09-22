#include "AutosaveJob.h"

#include <glib.h>  // for g_message, g_warning

#include "control/Control.h"              // for Control
#include "control/jobs/Job.h"             // for JOB_TYPE_AUTOSAVE, JobType
#include "control/xojfile/SaveHandler.h"  // for SaveHandler
#include "model/Document.h"               // for Document
#include "undo/UndoRedoHandler.h"         // for UndoRedoHandler
#include "util/PathUtil.h"                // for clearExtensions, getAutosav...
#include "util/i18n.h"                    // for FS, _F

#include "filesystem.h"  // for path

AutosaveJob::AutosaveJob(Control* control): control(control) {}

AutosaveJob::~AutosaveJob() = default;

void AutosaveJob::afterRun() {
    /*
     * Plan 004: an autosave failure no longer interrupts the user with a modal dialog. It is
     * reported to the safety state, which keeps it visible until it is dealt with and offers the
     * details behind one control - and it is still logged, and still an error, not a warning that
     * is quietly dropped.
     */
    if (!this->error.empty()) {
        g_warning("Autosave failed: %s", this->error.c_str());
        this->control->getSafetyState()->autosaveFailed(this->error);
        return;
    }

    this->control->getSafetyState()->autosaveSucceeded(this->recoveryFile);
}

void AutosaveJob::run() {
    SaveHandler handler;

    Document* doc = control->getDocument();

    doc->lock_shared();
    auto filepath = doc->getFilepath();

    if (filepath.empty()) {
        filepath = Util::getAutosaveFilepath();
    } else {
        filepath.replace_filename(fs::path(".") += filepath.filename());
    }
    Util::clearExtensions(filepath);
    filepath += ".autosave.xopp";

    handler.prepareSave(doc, filepath);
    doc->unlock_shared();

    g_message("%s", FS(_F("Autosaving to {1}") % filepath.string()).c_str());

    fs::path tempfile = filepath;
    tempfile += u8"~";
    handler.saveTo(tempfile);

    doc->lock();
    handler.updateDocumentInfo(doc);
    doc->unlock();

    this->error = handler.getErrorMessage();
    if (this->error.empty()) {
        try {
            if (fs::exists(filepath)) {
                fs::path swaptmpfile = filepath;
                swaptmpfile += u8".swap";
                Util::safeRenameFile(filepath, swaptmpfile);
                Util::safeRenameFile(tempfile, filepath);
                // All went well, we can delete the old autosave file
                fs::remove(swaptmpfile);
            } else {
                Util::safeRenameFile(tempfile, filepath);
            }
            control->setLastAutosaveFile(filepath);
            this->recoveryFile = filepath;

            /*
             * Plan 004: the undo position is recorded only now, when the copy is really on disk.
             * Recording it before the write would make a failed autosave look autosaved, and the
             * next tick would then skip the retry that the user needs.
             */
            control->getUndoRedoHandler()->documentAutosaved();
        } catch (const fs::filesystem_error& e) {
            auto fmtstr = _F("Could not rename autosave file from \"{1}\" to \"{2}\": {3}");
            this->error = FS(fmtstr % tempfile.u8string() % filepath.u8string() % e.what());
        }
    }

    // The outcome is reported on the UI thread, whether it is a success or a failure.
    callAfterRun();
}

auto AutosaveJob::getType() -> JobType { return JOB_TYPE_AUTOSAVE; }
