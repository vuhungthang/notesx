/*
 * Xournal++
 *
 * Autosave job
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>  // for string

#include "Job.h"  // for Job, JobType

#include "filesystem.h"  // for path

class Control;

class AutosaveJob: public Job {
public:
    AutosaveJob(Control* control);

protected:
    ~AutosaveJob() override;

public:
    void run() override;
    void afterRun() override;

    JobType getType() override;

private:
    Control* control = nullptr;
    std::string error;
    /// The recovery file, once it has really been written. Empty when nothing was written.
    fs::path recoveryFile;
};
