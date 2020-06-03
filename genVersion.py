import subprocess

git_commit = subprocess.check_output(['git', 'describe', '--tags', '--always', '--dirty']).strip() 


print("-DVERSION=\"%s\"" % git_commit)