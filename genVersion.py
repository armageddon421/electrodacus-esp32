import subprocess

git_commit = subprocess.check_output(['git', 'describe', '--tags', '--always', '--dirty']).strip() 


print("-DGIT_VERSION=\"%s\"" % git_commit)