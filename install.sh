python setup.py clean --all
python setup.py build_ext --inplace
python setup.py install
pip install -e .