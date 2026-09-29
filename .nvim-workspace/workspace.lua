local nmap = require("config.utils").nmap

nmap("<leader>e", ":sp term://just build<cr>")
nmap("<leader>E", ":sp term://just run<cr>")
